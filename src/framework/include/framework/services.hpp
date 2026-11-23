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

constexpr uint8_t STATS_MAX_CORES = 2U; /**< Số core tối đa trong SystemStats. */

/**
 * @brief Chỉ số vận hành tức thời (bộ nhớ, CPU, lưu trữ, nhiệt độ, sóng Wi-Fi).
 *
 * Mỗi nhóm có cờ has_* = false khi board không hỗ trợ; khi đó giá trị nhóm bằng 0.
 */
struct SystemStats {
    bool has_memory;
    uint32_t ram_total;   /**< Heap RAM nội (byte). */
    uint32_t ram_free;
    uint32_t ram_min;     /**< Thấp nhất từng ghi nhận. */
    uint32_t ram_largest; /**< Khối liền lớn nhất còn cấp được. */
    uint32_t psram_total; /**< 0 nếu không có PSRAM. */
    uint32_t psram_free;
    uint32_t psram_min;

    bool has_cpu;
    uint8_t cpu_cores;
    uint8_t cpu_load_pct[STATS_MAX_CORES]; /**< 0..100, tính trên khoảng giữa hai lần đọc. */
    uint16_t task_count;
    uint32_t cpu_freq_mhz;

    bool has_firmware;
    uint32_t flash_total;   /**< Chip flash (byte). */
    uint32_t app_partition; /**< Phân vùng firmware đang chạy. */
    uint32_t app_used;      /**< Kích thước firmware. */

    bool has_sd;
    uint64_t sd_total; /**< Thẻ nhớ (byte). */
    uint64_t sd_free;

    bool has_temperature;
    float temperature_c; /**< Nhiệt độ chip. */

    bool has_wifi_rssi;
    int8_t wifi_rssi; /**< dBm của AP đang kết nối. */
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

    /**
     * @brief Đọc chỉ số vận hành. Gọi khoảng 1 lần/giây là đủ (tải CPU tính theo khoảng giữa hai lần đọc).
     * @note  Lần đầu có thể chậm vài trăm ms (đếm dung lượng trống thẻ SD).
     */
    void system_stats(SystemStats& stats) const;

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
