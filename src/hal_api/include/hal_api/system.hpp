/**
 * @file    system.hpp
 * @brief   HAL contract: thông tin & điều khiển hệ thống (chip, heap, reset).
 */
#ifndef HAL_API_SYSTEM_HPP
#define HAL_API_SYSTEM_HPP

#include <cstdint>

#include "common/status.hpp"

namespace hal {

constexpr uint8_t SYSTEM_MAX_CORES = 2U; /**< Số core tối đa CpuStats theo dõi. */

/** @brief Thông tin tĩnh/động của nền tảng. */
struct SystemInfo {
    const char* p_chip_name;  /**< VD: "ESP32-D0WD". */
    uint32_t cpu_freq_mhz;    /**< Tần số CPU (MHz). */
    uint8_t cpu_cores;        /**< Số core. */
    uint32_t flash_size_kb;   /**< Dung lượng flash (KiB). */
    uint32_t heap_free_bytes; /**< Heap còn trống tại thời điểm đọc. */
    uint32_t heap_min_bytes;  /**< Heap thấp nhất từng ghi nhận (watermark). */
};

/** @brief Thống kê bộ nhớ heap (byte). PSRAM = 0 nếu board không có. */
struct MemoryStats {
    uint32_t internal_total;   /**< Tổng heap RAM nội. */
    uint32_t internal_free;    /**< Còn trống. */
    uint32_t internal_min;     /**< Thấp nhất từng ghi nhận. */
    uint32_t internal_largest; /**< Khối liền lớn nhất còn cấp được (độ phân mảnh). */
    uint32_t psram_total;      /**< Tổng heap PSRAM. */
    uint32_t psram_free;
    uint32_t psram_min;
};

/** @brief Tải CPU theo core, tính trên khoảng giữa hai lần đọc. */
struct CpuStats {
    uint8_t core_count;                  /**< Số core có số liệu. */
    uint8_t load_pct[SYSTEM_MAX_CORES];  /**< 0..100 (%). */
    uint16_t task_count;                 /**< Số task RTOS đang tồn tại. */
};

/** @brief Flash và firmware (byte). */
struct FirmwareStats {
    uint32_t flash_total;   /**< Dung lượng chip flash. */
    uint32_t app_partition; /**< Kích thước phân vùng app đang chạy. */
    uint32_t app_used;      /**< Kích thước image firmware trong phân vùng. */
};

/** @brief Dịch vụ hệ thống do platform cung cấp. */
class ISystem {
public:
    virtual ~ISystem() = default;

    /** @param[out] info Thông tin hệ thống hiện tại. */
    virtual void get_info(SystemInfo& info) const = 0;

    /** @brief Khởi động lại MCU (không return). */
    virtual void restart() = 0;

    /** @param[out] stats Heap RAM nội / PSRAM. @return ERR_NOT_SUPPORTED nếu platform không hỗ trợ. */
    virtual common::Status get_memory_stats(MemoryStats& stats) const
    {
        (void)stats;
        return common::Status::ERR_NOT_SUPPORTED;
    }

    /**
     * @brief Tải CPU từng core kể từ lần đọc trước (lần đầu: kể từ lúc khởi động).
     * @note  Đọc dày hơn chu kỳ lấy mẫu nội bộ sẽ trả lại kết quả đã có (nhiều người gọi không làm nhiễu nhau).
     */
    virtual common::Status get_cpu_stats(CpuStats& stats)
    {
        (void)stats;
        return common::Status::ERR_NOT_SUPPORTED;
    }

    /** @param[out] stats Dung lượng flash, phân vùng app, kích thước firmware. */
    virtual common::Status get_firmware_stats(FirmwareStats& stats) const
    {
        (void)stats;
        return common::Status::ERR_NOT_SUPPORTED;
    }

    /** @param[out] celsius Nhiệt độ chip (°C). */
    virtual common::Status read_temperature(float& celsius)
    {
        (void)celsius;
        return common::Status::ERR_NOT_SUPPORTED;
    }
};

} // namespace hal

#endif /* HAL_API_SYSTEM_HPP */
