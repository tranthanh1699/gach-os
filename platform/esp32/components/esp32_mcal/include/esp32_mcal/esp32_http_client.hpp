/**
 * @file    esp32_http_client.hpp
 * @brief   hal::IHttpClient bằng esp_http_client + bundle chứng chỉ mbedTLS, chạy trên task riêng.
 */
#ifndef ESP32_MCAL_ESP32_HTTP_CLIENT_HPP
#define ESP32_MCAL_ESP32_HTTP_CLIENT_HPP

#include <atomic>
#include <cstddef>
#include <cstdint>

#include "common/non_copyable.hpp"
#include "freertos/FreeRTOS.h"
#include "freertos/semphr.h"
#include "hal_api/http.hpp"

namespace esp32_mcal {

/** @brief Một kênh GET; task worker ngủ trên semaphore tới khi có yêu cầu. */
class Esp32HttpClient final : public hal::IHttpClient, private common::NonCopyable {
public:
    Esp32HttpClient();

    /**
     * @brief Cấp bộ đệm phản hồi (PSRAM nếu có) và tạo task worker.
     * @param[in] body_capacity Kích thước bộ đệm phản hồi (byte).
     */
    common::Status init(size_t body_capacity);

    common::Status start_get(const char* p_url) override;
    hal::HttpState state() const override { return static_cast<hal::HttpState>(m_state.load()); }
    hal::HttpResult result() const override { return m_result; }
    const uint8_t* body() const override { return m_p_body; }
    size_t capacity() const override { return m_capacity; }
    void release() override;

private:
    static void task_entry(void* p_arg);
    void perform();
    void append(const void* p_data, size_t len);

    uint8_t* m_p_body;
    size_t m_capacity;
    char m_url[hal::HTTP_URL_MAX_LEN];
    hal::HttpResult m_result;
    SemaphoreHandle_t m_p_start;
    StaticSemaphore_t m_start_storage;
    std::atomic<uint8_t> m_state;

    friend struct HttpEventBridge;
};

} // namespace esp32_mcal

#endif /* ESP32_MCAL_ESP32_HTTP_CLIENT_HPP */
