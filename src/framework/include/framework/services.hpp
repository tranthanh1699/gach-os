/**
 * @file    services.hpp
 * @brief   Facade dịch vụ hệ thống cho app. App KHÔNG được chạm HAL – chỉ đi qua đây.
 */
#ifndef FRAMEWORK_SERVICES_HPP
#define FRAMEWORK_SERVICES_HPP

#include <cstdint>

#include "common/non_copyable.hpp"
#include "common/status.hpp"
#include "framework/time_service.hpp"
#include "framework/wifi_service.hpp"

namespace hal {
struct Devices; // Forward declaration: header này không lộ HAL cho tầng app.
}

namespace fw {

/** @brief Thông tin thiết bị dạng hiển thị cho app (không phụ thuộc kiểu HAL). */
struct DeviceInfo {
    const char* p_board_name;
    const char* p_chip_name;
    uint32_t cpu_freq_mhz;
    uint8_t cpu_cores;
    uint32_t flash_size_kb;
    uint32_t heap_free_bytes;
    uint32_t heap_min_bytes;
    uint16_t screen_width;
    uint16_t screen_height;
    bool has_touch;
};

/** @brief Dịch vụ hệ thống. */
class Services : private common::NonCopyable {
public:
    Services() : m_p_devices(nullptr) {}

    /** @brief [Nội bộ framework] Gắn tập thiết bị của board (kể cả các dịch vụ con). */
    void bind(const hal::Devices& devices);

    /** @return Dịch vụ Wi-Fi. */
    WifiService& wifi() { return m_wifi; }
    const WifiService& wifi() const { return m_wifi; }

    /** @return Dịch vụ thời gian. */
    TimeService& time() { return m_time; }
    const TimeService& time() const { return m_time; }

    /** @return true nếu board có đèn nền điều chỉnh được. */
    bool has_backlight() const;

    /** @return Độ sáng hiện tại (%), 0 nếu không có backlight. */
    uint8_t brightness() const;

    /** @brief Đặt độ sáng (%). @return ERR_NOT_SUPPORTED nếu không có backlight. */
    common::Status set_brightness(uint8_t percent);

    /** @param[out] info Thông tin thiết bị hiện tại. */
    void device_info(DeviceInfo& info) const;

    /** @return Thời gian từ lúc khởi động (ms). */
    uint32_t uptime_ms() const;

    /** @brief Khởi động lại thiết bị. */
    void restart();

private:
    const hal::Devices* m_p_devices;
    WifiService m_wifi;
    TimeService m_time;
};

} // namespace fw

#endif /* FRAMEWORK_SERVICES_HPP */
