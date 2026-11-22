/**
 * @file    esp32_clock.hpp
 * @brief   hal::IClock trên ESP-IDF: đồng hồ hệ thống + SNTP (esp_netif_sntp).
 */
#ifndef ESP32_MCAL_ESP32_CLOCK_HPP
#define ESP32_MCAL_ESP32_CLOCK_HPP

#include "common/non_copyable.hpp"
#include "hal_api/clock.hpp"

namespace esp32_mcal {

/** @brief Đồng hồ UTC, đồng bộ SNTP định kỳ (CONFIG_LWIP_SNTP_UPDATE_DELAY). */
class Esp32Clock final : public hal::IClock, private common::NonCopyable {
public:
    Esp32Clock() : m_is_started(false) {}

    /** @note Yêu cầu esp_netif đã init (Esp32Wifi::init). */
    common::Status start_sync(const char* p_server) override;
    bool is_synced() const override;
    bool utc_seconds(int64_t& seconds) const override;

private:
    bool m_is_started;
};

} // namespace esp32_mcal

#endif /* ESP32_MCAL_ESP32_CLOCK_HPP */
