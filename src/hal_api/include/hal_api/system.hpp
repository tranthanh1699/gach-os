/**
 * @file    system.hpp
 * @brief   HAL contract: thông tin & điều khiển hệ thống (chip, heap, reset).
 */
#ifndef HAL_API_SYSTEM_HPP
#define HAL_API_SYSTEM_HPP

#include <cstdint>

namespace hal {

/** @brief Thông tin tĩnh/động của nền tảng. */
struct SystemInfo {
    const char* p_chip_name;  /**< VD: "ESP32-D0WD". */
    uint32_t cpu_freq_mhz;    /**< Tần số CPU (MHz). */
    uint8_t cpu_cores;        /**< Số core. */
    uint32_t flash_size_kb;   /**< Dung lượng flash (KiB). */
    uint32_t heap_free_bytes; /**< Heap còn trống tại thời điểm đọc. */
    uint32_t heap_min_bytes;  /**< Heap thấp nhất từng ghi nhận (watermark). */
};

/** @brief Dịch vụ hệ thống do platform cung cấp. */
class ISystem {
public:
    virtual ~ISystem() = default;

    /** @param[out] info Thông tin hệ thống hiện tại. */
    virtual void get_info(SystemInfo& info) const = 0;

    /** @brief Khởi động lại MCU (không return). */
    virtual void restart() = 0;
};

} // namespace hal

#endif /* HAL_API_SYSTEM_HPP */
