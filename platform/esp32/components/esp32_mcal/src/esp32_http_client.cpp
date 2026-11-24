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
#include "psa/crypto.h"

namespace esp32_mcal {

namespace {
constexpr const char* TAG = "http";
constexpr uint32_t TASK_STACK_BYTES = 8U * 1024U; /**< Bắt tay TLS (mbedTLS) cần stack sâu. */
constexpr UBaseType_t TASK_PRIORITY = 3U;
constexpr BaseType_t TASK_CORE = 0; /**< Bắt tay TLS tốn CPU: giữ ở core 0, UI chạy core 1. */
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
            if (p_self->m_p_fs != nullptr) {
                p_self->write_chunk(p_event->client, p_event->data, static_cast<size_t>(p_event->data_len));
            } else {
                p_self->append(p_event->data, static_cast<size_t>(p_event->data_len));
            }
        }
        return ESP_OK;
    }
};

namespace {
constexpr int HTTP_OK = 200;
psa_hash_operation_t s_hash; /**< Một kênh yêu cầu duy nhất -> một phép băm. */
} // namespace

Esp32HttpClient::Esp32HttpClient()
    : m_p_body(nullptr), m_capacity(0U), m_url{}, m_result{}, m_p_start(nullptr), m_start_storage{},
      m_state(static_cast<uint8_t>(hal::HttpState::IDLE)), m_p_fs(nullptr), m_path{}, m_file(hal::INVALID_FILE),
      m_received(0U), m_total(0U), m_write_failed(false), m_has_digest(false), m_digest{}, m_p_hash(&s_hash)
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
    if (xTaskCreatePinnedToCore(&Esp32HttpClient::task_entry, "http", TASK_STACK_BYTES, this, TASK_PRIORITY, nullptr,
                                (TASK_CORE < portNUM_PROCESSORS) ? TASK_CORE : tskNO_AFFINITY) != pdPASS) {
        heap_caps_free(m_p_body);
        m_p_body = nullptr;
        return common::Status::ERR_NO_MEM;
    }
    return common::Status::OK;
}

common::Status Esp32HttpClient::start_get(const char* p_url)
{
    return start(p_url, nullptr, nullptr);
}

common::Status Esp32HttpClient::start_download(const char* p_url, hal::IFileSystem& fs, const char* p_path)
{
    if ((p_path == nullptr) || (p_path[0] == '\0') || (std::strlen(p_path) >= sizeof(m_path))) {
        return common::Status::ERR_PARAM;
    }
    return start(p_url, &fs, p_path);
}

void Esp32HttpClient::download_progress(size_t& received, size_t& total) const
{
    received = m_received.load();
    total = m_total.load();
}

bool Esp32HttpClient::download_sha256(uint8_t (&out)[hal::HTTP_SHA256_LEN]) const
{
    if (!m_has_digest || (state() != hal::HttpState::DONE)) {
        return false;
    }
    std::memcpy(out, m_digest, sizeof(out));
    return true;
}

void Esp32HttpClient::write_chunk(void* p_client, const void* p_data, size_t len)
{
    auto* const p_http = static_cast<esp_http_client_handle_t>(p_client);
    if (esp_http_client_get_status_code(p_http) != HTTP_OK) {
        return; // Body của 3xx (chuyển hướng) / lỗi: không ghi vào file.
    }
    if (m_total.load() == 0U) {
        const int64_t length = esp_http_client_get_content_length(p_http);
        m_total.store((length > 0) ? static_cast<uint32_t>(length) : 0U);
    }
    if (m_write_failed) {
        return;
    }
    if (!common::is_ok(m_p_fs->write(m_file, static_cast<const uint8_t*>(p_data), len))) {
        m_write_failed = true;
        return;
    }
    (void)psa_hash_update(static_cast<psa_hash_operation_t*>(m_p_hash), static_cast<const uint8_t*>(p_data), len);
    m_received.fetch_add(static_cast<uint32_t>(len));
}

common::Status Esp32HttpClient::start(const char* p_url, hal::IFileSystem* p_fs, const char* p_path)
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
    m_p_fs = p_fs;
    if (p_path != nullptr) {
        std::strcpy(m_path, p_path);
    }
    m_received.store(0U);
    m_total.store(0U);
    m_write_failed = false;
    m_has_digest = false;
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

    const bool is_download = (m_p_fs != nullptr);
    if (is_download) {
        if (!common::is_ok(m_p_fs->open(m_path, hal::OpenMode::WRITE, m_file))) {
            m_result.status = common::Status::ERR_IO;
            m_p_fs = nullptr;
            return;
        }
        (void)psa_crypto_init(); // Gọi lại nhiều lần an toàn.
        *static_cast<psa_hash_operation_t*>(m_p_hash) = psa_hash_operation_init();
        (void)psa_hash_setup(static_cast<psa_hash_operation_t*>(m_p_hash), PSA_ALG_SHA_256);
    }
    esp_http_client_handle_t p_client = esp_http_client_init(&cfg);
    if (p_client == nullptr) {
        m_result.status = common::Status::ERR_NO_MEM;
    } else {
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
    if (is_download) {
        m_p_fs->close(m_file);
        m_file = hal::INVALID_FILE;
        size_t digest_len = 0U;
        m_has_digest = (psa_hash_finish(static_cast<psa_hash_operation_t*>(m_p_hash), m_digest, sizeof(m_digest),
                                        &digest_len) == PSA_SUCCESS) &&
                       (digest_len == sizeof(m_digest));
        if (m_write_failed && common::is_ok(m_result.status)) {
            m_result.status = common::Status::ERR_IO; // Thẻ đầy / lỗi ghi.
        }
        m_result.body_len = m_received.load();
        m_p_fs = nullptr;
    }
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
