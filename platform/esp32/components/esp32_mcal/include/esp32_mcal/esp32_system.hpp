/**
 * @file    esp32_system.hpp
 * @brief   hal::ISystem cho họ ESP32 (thông tin chip, heap, tải CPU, flash, nhiệt độ, restart).
 */
#ifndef ESP32_MCAL_ESP32_SYSTEM_HPP
#define ESP32_MCAL_ESP32_SYSTEM_HPP

#include <cstdint>

#include "common/non_copyable.hpp"
#include "freertos/FreeRTOS.h"
#include "hal_api/system.hpp"

namespace esp32_mcal {

/** @brief Dịch vụ hệ thống ESP-IDF. */
class Esp32System final : public hal::ISystem, private common::NonCopyable {
public:
    Esp32System();

    void get_info(hal::SystemInfo& info) const override;
    void restart() override;
    common::Status get_memory_stats(hal::MemoryStats& stats) const override;
    common::Status get_cpu_stats(hal::CpuStats& stats) override;
    common::Status get_firmware_stats(hal::FirmwareStats& stats) const override;
    common::Status read_temperature(float& celsius) override;
    common::Status get_chip_details(hal::ChipDetails& details) const override;
    common::Status get_firmware_info(hal::FirmwareInfo& info) const override;
    common::Status get_boot_info(hal::BootInfo& info) const override;
    common::Status get_tasks(hal::TaskInfo* p_out, size_t max, size_t& count, uint64_t& total_runtime) const override;
    common::Status get_heap_regions(hal::HeapRegion* p_out, size_t max, size_t& count) const override;
    common::Status get_partitions(hal::FlashPartition* p_out, size_t max, size_t& count) const override;
    common::Status heap_trace_start(size_t max_records) override;
    common::Status heap_trace_stop() override;
    common::Status heap_trace_status(size_t& count, size_t& bytes, bool& is_running) const override;
    common::Status heap_trace_get(size_t index, hal::HeapTraceRecord& record) const override;

private:
    /** @brief Lấy mẫu bộ đếm idle mỗi core và tính tải (gọi trong vùng khóa). */
    void sample_cpu_locked(uint64_t now);

    portMUX_TYPE m_lock;
    uint64_t m_last_sample;                          /**< Mốc bộ đếm run-time lần lấy mẫu trước. */
    uint64_t m_last_idle[hal::SYSTEM_MAX_CORES];     /**< Bộ đếm idle từng core lần trước. */
    uint8_t m_load_pct[hal::SYSTEM_MAX_CORES];       /**< Kết quả gần nhất. */
    void* m_p_tsens;                                 /**< temperature_sensor_handle_t, tạo khi đọc lần đầu. */
    bool m_tsens_failed;                             /**< Cài cảm biến lỗi -> không thử lại mỗi lần đọc. */
    void* m_p_trace_buf;                             /**< Bộ đệm bản ghi heap trace (PSRAM), cấp khi dùng lần đầu. */
    size_t m_trace_capacity;
    bool m_trace_running;
};

} // namespace esp32_mcal

#endif /* ESP32_MCAL_ESP32_SYSTEM_HPP */
