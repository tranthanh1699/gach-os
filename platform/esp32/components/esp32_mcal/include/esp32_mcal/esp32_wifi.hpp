/**
 * @file    esp32_wifi.hpp
 * @brief   hal::IWifi trên ESP-IDF (esp_wifi station + esp_event).
 *
 * Event handler chạy trong task "sys_evt" của ESP-IDF; trạng thái chia sẻ với UI task
 * qua std::atomic và spinlock cho mảng kết quả quét.
 */
#ifndef ESP32_MCAL_ESP32_WIFI_HPP
#define ESP32_MCAL_ESP32_WIFI_HPP

#include <atomic>
#include <cstddef>
#include <cstdint>

#include "common/non_copyable.hpp"
#include "esp_event.h"
#include "freertos/FreeRTOS.h"
#include "hal_api/wifi.hpp"

namespace esp32_mcal {

/** @brief Wi-Fi station ESP32. */
class Esp32Wifi final : public hal::IWifi, private common::NonCopyable {
public:
    static constexpr size_t MAX_SCAN_RESULTS = 16U;
    static constexpr uint8_t MAX_RETRIES = 5U; /**< Số lần thử lại trước khi báo FAILED. */

    Esp32Wifi();

    /** @brief Khởi tạo netif + Wi-Fi driver ở chế độ STA. Yêu cầu NVS đã init. */
    common::Status init();

    common::Status start_scan() override;
    bool is_scanning() const override { return m_is_scanning.load(); }
    uint32_t scan_generation() const override { return m_scan_generation.load(); }
    size_t scan_results(hal::WifiAp* p_out, size_t max_count) const override;
    common::Status connect(const char* p_ssid, const char* p_password) override;
    common::Status disconnect() override;
    hal::WifiState state() const override { return static_cast<hal::WifiState>(m_state.load()); }
    bool ip_address(char* p_buf, size_t buf_len) const override;

private:
    static void event_handler(void* p_arg, esp_event_base_t base, int32_t event_id, void* p_data);
    void on_scan_done();
    void on_disconnected();
    void set_state(hal::WifiState state) { m_state.store(static_cast<uint8_t>(state)); }

    mutable portMUX_TYPE m_lock;
    hal::WifiAp m_results[MAX_SCAN_RESULTS];
    size_t m_result_count;
    std::atomic<uint8_t> m_state;
    std::atomic<bool> m_is_scanning;
    std::atomic<uint32_t> m_scan_generation;
    std::atomic<uint32_t> m_ip_addr;
    std::atomic<bool> m_want_connection;
    std::atomic<uint8_t> m_retries;
    bool m_is_initialized;
};

} // namespace esp32_mcal

#endif /* ESP32_MCAL_ESP32_WIFI_HPP */
