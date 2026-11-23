/**
 * @file    esp32_wifi.cpp
 * @brief   Hiện thực Wi-Fi station trên ESP-IDF.
 */
#include "esp32_mcal/esp32_wifi.hpp"

#include <algorithm>
#include <cstdio>
#include <cstring>

#include "common/string_utils.hpp"
#include "esp_netif.h"
#include "esp_wifi.h"

namespace esp32_mcal {

namespace {

// Chỉ truy cập trong task sự kiện của ESP-IDF (on_scan_done) -> không cần khóa.
wifi_ap_record_t s_ap_records[Esp32Wifi::MAX_SCAN_RESULTS];

void copy_bytes(uint8_t* p_dst, size_t dst_size, const char* p_src)
{
    const size_t length = std::min(std::strlen(p_src), dst_size);
    (void)std::memcpy(p_dst, p_src, length);
}

} // namespace

Esp32Wifi::Esp32Wifi()
    : m_results{}, m_result_count(0U), m_state(static_cast<uint8_t>(hal::WifiState::DISCONNECTED)),
      m_is_scanning(false), m_scan_generation(0U), m_ip_addr(0U), m_want_connection(false), m_retries(0U),
      m_is_initialized(false)
{
    portMUX_INITIALIZE(&m_lock);
}

common::Status Esp32Wifi::init()
{
    if (m_is_initialized) {
        return common::Status::ERR_STATE;
    }
    if (esp_netif_init() != ESP_OK) {
        return common::Status::ERR_IO;
    }
    const esp_err_t loop_err = esp_event_loop_create_default();
    if ((loop_err != ESP_OK) && (loop_err != ESP_ERR_INVALID_STATE)) { // INVALID_STATE = đã tạo trước đó.
        return common::Status::ERR_IO;
    }
    if (esp_netif_create_default_wifi_sta() == nullptr) {
        return common::Status::ERR_NO_MEM;
    }

    const wifi_init_config_t init_cfg = WIFI_INIT_CONFIG_DEFAULT();
    if ((esp_wifi_init(&init_cfg) != ESP_OK) ||
        (esp_event_handler_instance_register(WIFI_EVENT, ESP_EVENT_ANY_ID, &Esp32Wifi::event_handler, this,
                                             nullptr) != ESP_OK) ||
        (esp_event_handler_instance_register(IP_EVENT, IP_EVENT_STA_GOT_IP, &Esp32Wifi::event_handler, this,
                                             nullptr) != ESP_OK) ||
        // Thông tin đăng nhập do framework quản lý (IKeyValueStore) -> driver không tự lưu flash.
        (esp_wifi_set_storage(WIFI_STORAGE_RAM) != ESP_OK) || (esp_wifi_set_mode(WIFI_MODE_STA) != ESP_OK) ||
        (esp_wifi_start() != ESP_OK)) {
        return common::Status::ERR_IO;
    }
    m_is_initialized = true;
    return common::Status::OK;
}

common::Status Esp32Wifi::start_scan()
{
    if (!m_is_initialized) {
        return common::Status::ERR_STATE;
    }
    if (m_is_scanning.load() || (state() == hal::WifiState::CONNECTING)) {
        return common::Status::ERR_STATE;
    }
    wifi_scan_config_t scan_cfg{};
    scan_cfg.show_hidden = false;
    if (esp_wifi_scan_start(&scan_cfg, false) != ESP_OK) {
        return common::Status::ERR_IO;
    }
    m_is_scanning.store(true);
    return common::Status::OK;
}

size_t Esp32Wifi::scan_results(hal::WifiAp* p_out, size_t max_count) const
{
    if (p_out == nullptr) {
        return 0U;
    }
    taskENTER_CRITICAL(&m_lock);
    const size_t count = std::min(m_result_count, max_count);
    (void)std::memcpy(p_out, m_results, count * sizeof(hal::WifiAp));
    taskEXIT_CRITICAL(&m_lock);
    return count;
}

common::Status Esp32Wifi::connect(const char* p_ssid, const char* p_password)
{
    if (!m_is_initialized) {
        return common::Status::ERR_STATE;
    }
    if ((p_ssid == nullptr) || (p_password == nullptr)) {
        return common::Status::ERR_PARAM;
    }
    m_want_connection.store(false);
    (void)esp_wifi_disconnect(); // Ngắt kết nối cũ (nếu có); lỗi "chưa kết nối" là bình thường.

    wifi_config_t cfg{};
    copy_bytes(cfg.sta.ssid, sizeof(cfg.sta.ssid), p_ssid);
    copy_bytes(cfg.sta.password, sizeof(cfg.sta.password), p_password);
    cfg.sta.threshold.authmode = WIFI_AUTH_OPEN; // Chấp nhận mọi chế độ bảo mật mà AP hỗ trợ.
    cfg.sta.sae_pwe_h2e = WPA3_SAE_PWE_BOTH;
    if (esp_wifi_set_config(WIFI_IF_STA, &cfg) != ESP_OK) {
        return common::Status::ERR_PARAM;
    }

    m_retries.store(0U);
    m_ip_addr.store(0U);
    m_want_connection.store(true);
    set_state(hal::WifiState::CONNECTING);
    if (esp_wifi_connect() != ESP_OK) {
        m_want_connection.store(false);
        set_state(hal::WifiState::FAILED);
        return common::Status::ERR_IO;
    }
    return common::Status::OK;
}

common::Status Esp32Wifi::disconnect()
{
    if (!m_is_initialized) {
        return common::Status::ERR_STATE;
    }
    m_want_connection.store(false);
    m_ip_addr.store(0U);
    set_state(hal::WifiState::DISCONNECTED);
    (void)esp_wifi_disconnect();
    return common::Status::OK;
}

bool Esp32Wifi::ip_address(char* p_buf, size_t buf_len) const
{
    const uint32_t addr = m_ip_addr.load();
    if ((addr == 0U) || (p_buf == nullptr) || (buf_len == 0U)) {
        return false;
    }
    esp_ip4_addr_t ip{};
    ip.addr = addr;
    (void)std::snprintf(p_buf, buf_len, IPSTR, IP2STR(&ip));
    return true;
}

bool Esp32Wifi::link_rssi(int8_t& rssi) const
{
    if (state() != hal::WifiState::CONNECTED) {
        return false;
    }
    wifi_ap_record_t ap{};
    if (esp_wifi_sta_get_ap_info(&ap) != ESP_OK) {
        return false;
    }
    rssi = ap.rssi;
    return true;
}

void Esp32Wifi::event_handler(void* p_arg, esp_event_base_t base, int32_t event_id, void* p_data)
{
    auto* const p_self = static_cast<Esp32Wifi*>(p_arg);
    if (base == WIFI_EVENT) {
        if (event_id == WIFI_EVENT_SCAN_DONE) {
            p_self->on_scan_done();
        } else if (event_id == WIFI_EVENT_STA_DISCONNECTED) {
            p_self->on_disconnected();
        } else {
            // Các sự kiện khác không cần xử lý.
        }
    } else if ((base == IP_EVENT) && (event_id == IP_EVENT_STA_GOT_IP)) {
        const auto* const p_event = static_cast<const ip_event_got_ip_t*>(p_data);
        p_self->m_ip_addr.store(p_event->ip_info.ip.addr);
        p_self->m_retries.store(0U);
        p_self->set_state(hal::WifiState::CONNECTED);
    } else {
        // Không đăng ký base khác.
    }
}

void Esp32Wifi::on_scan_done()
{
    uint16_t count = static_cast<uint16_t>(MAX_SCAN_RESULTS);
    if (esp_wifi_scan_get_ap_records(&count, s_ap_records) != ESP_OK) {
        count = 0U;
    }
    taskENTER_CRITICAL(&m_lock);
    for (size_t index = 0U; index < count; ++index) {
        const wifi_ap_record_t& record = s_ap_records[index];
        hal::WifiAp& ap = m_results[index];
        (void)common::copy_string(ap.ssid, sizeof(ap.ssid), reinterpret_cast<const char*>(record.ssid));
        ap.rssi = record.rssi;
        ap.secure = (record.authmode != WIFI_AUTH_OPEN);
    }
    m_result_count = count;
    taskEXIT_CRITICAL(&m_lock);

    m_scan_generation.fetch_add(1U);
    m_is_scanning.store(false);
}

void Esp32Wifi::on_disconnected()
{
    m_ip_addr.store(0U);
    if (!m_want_connection.load()) {
        if (state() != hal::WifiState::FAILED) {
            set_state(hal::WifiState::DISCONNECTED);
        }
        return;
    }
    if (m_retries.load() < MAX_RETRIES) {
        m_retries.fetch_add(1U);
        set_state(hal::WifiState::CONNECTING);
        (void)esp_wifi_connect();
    } else {
        m_want_connection.store(false);
        set_state(hal::WifiState::FAILED);
    }
}

} // namespace esp32_mcal
