/**
 * @file    wifi_service.cpp
 * @brief   Hiện thực dịch vụ Wi-Fi.
 */
#include "framework/wifi_service.hpp"

#include <cstring>

#include "common/string_utils.hpp"
#include "hal_api/board.hpp"
#include "storage_keys.hpp"

namespace fw {

namespace {

WifiStatus to_status(hal::WifiState state)
{
    switch (state) {
    case hal::WifiState::CONNECTING:
        return WifiStatus::CONNECTING;
    case hal::WifiState::CONNECTED:
        return WifiStatus::CONNECTED;
    case hal::WifiState::FAILED:
        return WifiStatus::FAILED;
    case hal::WifiState::DISCONNECTED:
    default:
        return WifiStatus::DISCONNECTED;
    }
}

} // namespace

WifiService::WifiService()
    : m_p_devices(nullptr), m_status(WifiStatus::UNAVAILABLE), m_networks{}, m_network_count(0U),
      m_hal_scan_generation(0U), m_scan_generation(0U), m_ssid{}, m_pending_password{}, m_remember_pending(false)
{
}

bool WifiService::is_available() const
{
    return (m_p_devices != nullptr) && (m_p_devices->p_wifi != nullptr);
}

common::Status WifiService::scan()
{
    return is_available() ? m_p_devices->p_wifi->start_scan() : common::Status::ERR_NOT_SUPPORTED;
}

bool WifiService::is_scanning() const
{
    return is_available() && m_p_devices->p_wifi->is_scanning();
}

const WifiNetwork* WifiService::network_at(size_t index) const
{
    return (index < m_network_count) ? &m_networks[index] : nullptr;
}

common::Status WifiService::connect(const char* p_ssid, const char* p_password, bool remember)
{
    if (!is_available()) {
        return common::Status::ERR_NOT_SUPPORTED;
    }
    if ((p_ssid == nullptr) || (p_ssid[0] == '\0') || (std::strlen(p_ssid) > hal::WIFI_SSID_MAX_LEN) ||
        ((p_password != nullptr) && (std::strlen(p_password) > hal::WIFI_PASSWORD_MAX_LEN))) {
        return common::Status::ERR_PARAM;
    }
    const common::Status status = m_p_devices->p_wifi->connect(p_ssid, (p_password != nullptr) ? p_password : "");
    if (common::is_ok(status)) {
        (void)common::copy_string(m_ssid, sizeof(m_ssid), p_ssid);
        (void)common::copy_string(m_pending_password, sizeof(m_pending_password), p_password);
        m_remember_pending = remember;
        m_status = WifiStatus::CONNECTING;
    }
    return status;
}

common::Status WifiService::disconnect(bool forget)
{
    if (!is_available()) {
        return common::Status::ERR_NOT_SUPPORTED;
    }
    m_remember_pending = false;
    if (forget && (m_p_devices->p_storage != nullptr)) {
        (void)m_p_devices->p_storage->erase(storage_key::WIFI_SSID);
        (void)m_p_devices->p_storage->erase(storage_key::WIFI_PASSWORD);
    }
    const common::Status status = m_p_devices->p_wifi->disconnect();
    if (common::is_ok(status)) {
        m_ssid[0] = '\0';
        m_status = WifiStatus::DISCONNECTED;
    }
    return status;
}

bool WifiService::ip_address(char* p_buf, size_t buf_len) const
{
    return is_available() && m_p_devices->p_wifi->ip_address(p_buf, buf_len);
}

void WifiService::start()
{
    if (!is_available()) {
        m_status = WifiStatus::UNAVAILABLE;
        return;
    }
    m_status = WifiStatus::DISCONNECTED;
    hal::IKeyValueStore* const p_store = m_p_devices->p_storage;
    if (p_store == nullptr) {
        return;
    }
    char ssid[WIFI_SSID_BUF_LEN] = {};
    char password[WIFI_PASSWORD_BUF_LEN] = {};
    if (common::is_ok(p_store->get_string(storage_key::WIFI_SSID, ssid, sizeof(ssid))) && (ssid[0] != '\0')) {
        (void)p_store->get_string(storage_key::WIFI_PASSWORD, password, sizeof(password));
        (void)connect(ssid, password, false); // Đã lưu rồi -> không cần ghi lại.
    }
}

bool WifiService::poll()
{
    if (!is_available()) {
        return false;
    }
    const uint32_t generation = m_p_devices->p_wifi->scan_generation();
    if (generation != m_hal_scan_generation) {
        m_hal_scan_generation = generation;
        refresh_networks();
    }

    const WifiStatus previous = m_status;
    m_status = to_status(m_p_devices->p_wifi->state());
    const bool just_connected = (m_status == WifiStatus::CONNECTED) && (previous != WifiStatus::CONNECTED);
    if (just_connected && m_remember_pending) {
        save_credentials();
    }
    if (m_status != WifiStatus::CONNECTING) {
        // Không giữ mật khẩu trong RAM lâu hơn cần thiết.
        (void)std::memset(m_pending_password, 0, sizeof(m_pending_password));
        m_remember_pending = false;
    }
    return just_connected;
}

void WifiService::refresh_networks()
{
    hal::WifiAp results[config::WIFI_MAX_NETWORKS] = {};
    const size_t found = m_p_devices->p_wifi->scan_results(results, config::WIFI_MAX_NETWORKS);

    m_network_count = 0U;
    for (size_t index = 0U; index < found; ++index) {
        const hal::WifiAp& ap = results[index];
        bool duplicate = (ap.ssid[0] == '\0'); // Bỏ mạng ẩn SSID.
        for (size_t kept = 0U; (kept < m_network_count) && !duplicate; ++kept) {
            duplicate = (std::strcmp(m_networks[kept].ssid, ap.ssid) == 0);
        }
        if (!duplicate) {
            // Kết quả HAL đã sắp theo RSSI giảm dần -> bản đầu tiên là mạnh nhất.
            WifiNetwork& net = m_networks[m_network_count];
            (void)common::copy_string(net.ssid, sizeof(net.ssid), ap.ssid);
            net.rssi = ap.rssi;
            net.secure = ap.secure;
            m_network_count++;
        }
    }
    m_scan_generation++;
}

void WifiService::save_credentials()
{
    hal::IKeyValueStore* const p_store = m_p_devices->p_storage;
    if (p_store != nullptr) {
        (void)p_store->set_string(storage_key::WIFI_SSID, m_ssid);
        (void)p_store->set_string(storage_key::WIFI_PASSWORD, m_pending_password);
    }
}

} // namespace fw
