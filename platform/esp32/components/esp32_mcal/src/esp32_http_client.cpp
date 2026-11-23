/**
 * @file    esp32_http_client.cpp
 * @brief   Hiện thực hal::IHttpClient trên ESP-IDF.
 */
#include "esp32_mcal/esp32_http_client.hpp"

#include <cstring>

#include "esp_crt_bundle.h"
#include "esp_heap_caps.h"
#include "esp_http_client.h"
#include "esp_log.h"

namespace esp32_mcal {

namespace {
constexpr const char* TAG = "http";
constexpr uint32_t TASK_STACK_BYTES = 8U * 1024U; /**< Bắt tay TLS (mbedTLS) cần stack sâu. */
constexpr UBaseType_t TASK_PRIORITY = 3U;
constexpr int TIMEOUT_MS = 15000;
constexpr int RX_BUFFER_BYTES = 2048;
constexpr int MAX_REDIRECTS = 3;
} // namespace

/** @brief Cầu nối callback C của esp_http_client vào thành viên private. */
struct HttpEventBridge {
    static esp_err_t on_event(esp_http_client_event_t* p_event)
    {
        auto* const p_self = static_cast<Esp32HttpClient*>(p_event->user_data);
        if ((p_event->event_id == HTTP_EVENT_ON_DATA) && (p_self != nullptr)) {
            p_self->append(p_event->data, static_cast<size_t>(p_event->data_len));
        }
        return ESP_OK;
    }
};

Esp32HttpClient::Esp32HttpClient()
    : m_p_body(nullptr), m_capacity(0U), m_url{}, m_result{}, m_p_start(nullptr), m_start_storage{},
      m_state(static_cast<uint8_t>(hal::HttpState::IDLE))
{
}

common::Status Esp32HttpClient::init(size_t body_capacity)
{
    if (m_p_body != nullptr) {
        return common::Status::ERR_STATE;
    }
    m_p_body = static_cast<uint8_t*>(heap_caps_malloc(body_capacity, MALLOC_CAP_SPIRAM | MALLOC_CAP_8BIT));
    if (m_p_body == nullptr) {
        m_p_body = static_cast<uint8_t*>(heap_caps_malloc(body_capacity, MALLOC_CAP_8BIT));
    }
    if (m_p_body == nullptr) {
        return common::Status::ERR_NO_MEM;
    }
    m_capacity = body_capacity;
    m_p_start = xSemaphoreCreateBinaryStatic(&m_start_storage);
    // Stack ở RAM nội (mặc định của xTaskCreate): TLS/flash không được chạy trên stack PSRAM.
    if (xTaskCreate(&Esp32HttpClient::task_entry, "http", TASK_STACK_BYTES, this, TASK_PRIORITY, nullptr) != pdPASS) {
        heap_caps_free(m_p_body);
        m_p_body = nullptr;
        return common::Status::ERR_NO_MEM;
    }
    return common::Status::OK;
}

common::Status Esp32HttpClient::start_get(const char* p_url)
{
    if ((p_url == nullptr) || (p_url[0] == '\0') || (std::strlen(p_url) >= sizeof(m_url))) {
        return common::Status::ERR_PARAM;
    }
    uint8_t expected = static_cast<uint8_t>(hal::HttpState::IDLE);
    if ((m_p_start == nullptr) ||
        !m_state.compare_exchange_strong(expected, static_cast<uint8_t>(hal::HttpState::RUNNING))) {
        return common::Status::ERR_STATE;
    }
    std::strcpy(m_url, p_url);
    m_result = hal::HttpResult{common::Status::ERR_IO, 0, 0U, false};
    (void)xSemaphoreGive(m_p_start);
    return common::Status::OK;
}

void Esp32HttpClient::release()
{
    uint8_t expected = static_cast<uint8_t>(hal::HttpState::DONE);
    (void)m_state.compare_exchange_strong(expected, static_cast<uint8_t>(hal::HttpState::IDLE));
}

void Esp32HttpClient::append(const void* p_data, size_t len)
{
    const size_t room = m_capacity - m_result.body_len;
    const size_t copy = (len < room) ? len : room;
    if (copy < len) {
        m_result.truncated = true;
    }
    std::memcpy(&m_p_body[m_result.body_len], p_data, copy);
    m_result.body_len += copy;
}

void Esp32HttpClient::perform()
{
    esp_http_client_config_t cfg{};
    cfg.url = m_url;
    cfg.method = HTTP_METHOD_GET;
    cfg.timeout_ms = TIMEOUT_MS;
    cfg.buffer_size = RX_BUFFER_BYTES;
    cfg.max_redirection_count = MAX_REDIRECTS;
    cfg.event_handler = &HttpEventBridge::on_event;
    cfg.user_data = this;
    cfg.crt_bundle_attach = esp_crt_bundle_attach;

    esp_http_client_handle_t p_client = esp_http_client_init(&cfg);
    if (p_client == nullptr) {
        m_result.status = common::Status::ERR_NO_MEM;
        return;
    }
    const esp_err_t err = esp_http_client_perform(p_client);
    if (err == ESP_OK) {
        m_result.status = common::Status::OK;
        m_result.http_code = esp_http_client_get_status_code(p_client);
    } else {
        m_result.status = (err == ESP_ERR_HTTP_EAGAIN) ? common::Status::ERR_TIMEOUT : common::Status::ERR_IO;
        ESP_LOGW(TAG, "GET failed: %s", esp_err_to_name(err));
    }
    (void)esp_http_client_cleanup(p_client);
}

void Esp32HttpClient::task_entry(void* p_arg)
{
    auto* const p_self = static_cast<Esp32HttpClient*>(p_arg);
    for (;;) {
        (void)xSemaphoreTake(p_self->m_p_start, portMAX_DELAY);
        p_self->perform();
        p_self->m_state.store(static_cast<uint8_t>(hal::HttpState::DONE));
    }
}

} // namespace esp32_mcal
