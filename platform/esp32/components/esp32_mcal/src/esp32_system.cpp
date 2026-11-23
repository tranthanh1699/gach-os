/**
 * @file    esp32_system.cpp
 * @brief   Hiện thực hal::ISystem trên ESP-IDF.
 *
 * Tải CPU: FreeRTOS run-time stats (CONFIG_FREERTOS_GENERATE_RUN_TIME_STATS) – tải = 100% - tỷ lệ thời gian
 * task idle của core đó chạy giữa hai lần lấy mẫu.
 */
#include "esp32_mcal/esp32_system.hpp"

#include "driver/temperature_sensor.h"
#include "esp_app_format.h"
#include "esp_chip_info.h"
#include "esp_flash.h"
#include "esp_heap_caps.h"
#include "esp_image_format.h"
#include "esp_ota_ops.h"
#include "esp_partition.h"
#include "esp_system.h"
#include "freertos/task.h"
#include "sdkconfig.h"

namespace esp32_mcal {

namespace {
constexpr uint32_t BYTES_PER_KIB = 1024U;
constexpr uint64_t PERCENT = 100U;
constexpr uint64_t MIN_SAMPLE_PERIOD = 250000U; /**< Đơn vị bộ đếm run-time (µs với esp_timer). */
constexpr int TSENS_RANGE_MIN_C = -10;
constexpr int TSENS_RANGE_MAX_C = 80;

uint64_t run_time_now()
{
#if CONFIG_FREERTOS_GENERATE_RUN_TIME_STATS
    return static_cast<uint64_t>(portGET_RUN_TIME_COUNTER_VALUE());
#else
    return 0U;
#endif
}

uint64_t idle_counter(uint8_t core)
{
#if CONFIG_FREERTOS_GENERATE_RUN_TIME_STATS
    return static_cast<uint64_t>(ulTaskGetIdleRunTimeCounterForCore(static_cast<BaseType_t>(core)));
#else
    (void)core;
    return 0U;
#endif
}

uint8_t core_count()
{
    return (portNUM_PROCESSORS < hal::SYSTEM_MAX_CORES) ? static_cast<uint8_t>(portNUM_PROCESSORS)
                                                       : hal::SYSTEM_MAX_CORES;
}
} // namespace

Esp32System::Esp32System()
    : m_lock(portMUX_INITIALIZER_UNLOCKED), m_last_sample(0U), m_last_idle{}, m_load_pct{}, m_p_tsens(nullptr),
      m_tsens_failed(false)
{
}

void Esp32System::get_info(hal::SystemInfo& info) const
{
    esp_chip_info_t chip{};
    esp_chip_info(&chip);

    uint32_t flash_bytes = 0U;
    if (esp_flash_get_size(nullptr, &flash_bytes) != ESP_OK) {
        flash_bytes = 0U;
    }

    info.p_chip_name = CONFIG_IDF_TARGET;
    info.cpu_freq_mhz = static_cast<uint32_t>(CONFIG_ESP_DEFAULT_CPU_FREQ_MHZ);
    info.cpu_cores = chip.cores;
    info.flash_size_kb = flash_bytes / BYTES_PER_KIB;
    info.heap_free_bytes = esp_get_free_heap_size();
    info.heap_min_bytes = esp_get_minimum_free_heap_size();
}

void Esp32System::restart()
{
    esp_restart();
}

common::Status Esp32System::get_memory_stats(hal::MemoryStats& stats) const
{
    constexpr uint32_t INTERNAL_CAPS = MALLOC_CAP_INTERNAL | MALLOC_CAP_8BIT;
    stats.internal_total = static_cast<uint32_t>(heap_caps_get_total_size(INTERNAL_CAPS));
    stats.internal_free = static_cast<uint32_t>(heap_caps_get_free_size(INTERNAL_CAPS));
    stats.internal_min = static_cast<uint32_t>(heap_caps_get_minimum_free_size(INTERNAL_CAPS));
    stats.internal_largest = static_cast<uint32_t>(heap_caps_get_largest_free_block(INTERNAL_CAPS));
    stats.psram_total = static_cast<uint32_t>(heap_caps_get_total_size(MALLOC_CAP_SPIRAM));
    stats.psram_free = static_cast<uint32_t>(heap_caps_get_free_size(MALLOC_CAP_SPIRAM));
    stats.psram_min = static_cast<uint32_t>(heap_caps_get_minimum_free_size(MALLOC_CAP_SPIRAM));
    return common::Status::OK;
}

void Esp32System::sample_cpu_locked(uint64_t now)
{
    const uint64_t elapsed = now - m_last_sample;
    for (uint8_t core = 0U; core < core_count(); ++core) {
        const uint64_t idle = idle_counter(core);
        const uint64_t idle_delta = idle - m_last_idle[core];
        m_last_idle[core] = idle;
        const uint64_t idle_pct = (elapsed == 0U) ? PERCENT : ((idle_delta * PERCENT) / elapsed);
        m_load_pct[core] = static_cast<uint8_t>((idle_pct >= PERCENT) ? 0U : (PERCENT - idle_pct));
    }
    m_last_sample = now;
}

common::Status Esp32System::get_cpu_stats(hal::CpuStats& stats)
{
#if !CONFIG_FREERTOS_GENERATE_RUN_TIME_STATS
    (void)stats;
    return common::Status::ERR_NOT_SUPPORTED;
#else
    const uint64_t now = run_time_now();
    taskENTER_CRITICAL(&m_lock);
    if ((now - m_last_sample) >= MIN_SAMPLE_PERIOD) {
        sample_cpu_locked(now);
    }
    stats.core_count = core_count();
    for (uint8_t core = 0U; core < hal::SYSTEM_MAX_CORES; ++core) {
        stats.load_pct[core] = m_load_pct[core];
    }
    taskEXIT_CRITICAL(&m_lock);
    stats.task_count = static_cast<uint16_t>(uxTaskGetNumberOfTasks());
    return common::Status::OK;
#endif
}

common::Status Esp32System::get_firmware_stats(hal::FirmwareStats& stats) const
{
    // Firmware không đổi khi đang chạy -> đọc header image một lần.
    static uint32_t s_app_used = 0U;
    static uint32_t s_app_partition = 0U;

    uint32_t flash_bytes = 0U;
    if (esp_flash_get_size(nullptr, &flash_bytes) != ESP_OK) {
        flash_bytes = 0U;
    }
    if (s_app_partition == 0U) {
        const esp_partition_t* const p_part = esp_ota_get_running_partition();
        if (p_part == nullptr) {
            return common::Status::ERR_NOT_FOUND;
        }
        const esp_partition_pos_t pos{p_part->address, p_part->size};
        esp_image_metadata_t meta{};
        s_app_used = (esp_image_get_metadata(&pos, &meta) == ESP_OK) ? meta.image_len : 0U;
        s_app_partition = p_part->size;
    }
    stats.flash_total = flash_bytes;
    stats.app_partition = s_app_partition;
    stats.app_used = s_app_used;
    return common::Status::OK;
}

common::Status Esp32System::read_temperature(float& celsius)
{
    if (m_tsens_failed) {
        return common::Status::ERR_NOT_SUPPORTED;
    }
    if (m_p_tsens == nullptr) {
        const temperature_sensor_config_t cfg = TEMPERATURE_SENSOR_CONFIG_DEFAULT(TSENS_RANGE_MIN_C, TSENS_RANGE_MAX_C);
        temperature_sensor_handle_t p_handle = nullptr;
        if ((temperature_sensor_install(&cfg, &p_handle) != ESP_OK) || (temperature_sensor_enable(p_handle) != ESP_OK)) {
            m_tsens_failed = true;
            return common::Status::ERR_IO;
        }
        m_p_tsens = p_handle;
    }
    return (temperature_sensor_get_celsius(static_cast<temperature_sensor_handle_t>(m_p_tsens), &celsius) == ESP_OK)
               ? common::Status::OK
               : common::Status::ERR_IO;
}

} // namespace esp32_mcal
