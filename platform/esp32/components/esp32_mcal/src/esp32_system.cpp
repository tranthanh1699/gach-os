/**
 * @file    esp32_system.cpp
 * @brief   Hiện thực hal::ISystem trên ESP-IDF.
 *
 * Tải CPU: FreeRTOS run-time stats (CONFIG_FREERTOS_GENERATE_RUN_TIME_STATS) – tải = 100% - tỷ lệ thời gian
 * task idle của core đó chạy giữa hai lần lấy mẫu.
 */
#include "esp32_mcal/esp32_system.hpp"

#include <cstdio>
#include <cstring>

#include "driver/temperature_sensor.h"
#include "esp_app_desc.h"
#include "esp_attr.h"
#include "esp_heap_trace.h"
#include "esp_mac.h"
#include "esp_psram.h"
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

// Đếm số lần khởi động: RTC no-init giữ qua reset mềm / panic / watchdog, mất khi cấp nguồn lại.
constexpr uint32_t BOOT_MAGIC = 0x6A61C401U;
RTC_NOINIT_ATTR uint32_t s_boot_magic;
RTC_NOINIT_ATTR uint32_t s_boot_count;

void count_boot()
{
    const esp_reset_reason_t reason = esp_reset_reason();
    if ((s_boot_magic != BOOT_MAGIC) || (reason == ESP_RST_POWERON) || (reason == ESP_RST_BROWNOUT)) {
        s_boot_magic = BOOT_MAGIC;
        s_boot_count = 0U;
    }
    ++s_boot_count;
}

constexpr size_t MAX_TASKS = 64U;
// Chụp danh sách task (chỉ bridge gọi, tuần tự). PSRAM: không chiếm RAM nội cho tính năng debug.
EXT_RAM_BSS_ATTR TaskStatus_t s_task_status[MAX_TASKS];
constexpr size_t TRACE_MAX_RECORDS = 1000U;

void copy_text(char* p_out, size_t len, const char* p_text)
{
    (void)std::snprintf(p_out, len, "%s", (p_text != nullptr) ? p_text : "");
}

const char* model_name(esp_chip_model_t model)
{
    switch (model) {
    case CHIP_ESP32: return "ESP32";
    case CHIP_ESP32S2: return "ESP32-S2";
    case CHIP_ESP32S3: return "ESP32-S3";
    case CHIP_ESP32C3: return "ESP32-C3";
    case CHIP_ESP32C2: return "ESP32-C2";
    case CHIP_ESP32C6: return "ESP32-C6";
    case CHIP_ESP32H2: return "ESP32-H2";
    case CHIP_ESP32P4: return "ESP32-P4";
    default: return CONFIG_IDF_TARGET;
    }
}

hal::ResetReason map_reset(esp_reset_reason_t reason)
{
    switch (reason) {
    case ESP_RST_POWERON: return hal::ResetReason::POWER_ON;
    case ESP_RST_EXT: return hal::ResetReason::EXTERNAL;
    case ESP_RST_SW: return hal::ResetReason::SOFTWARE;
    case ESP_RST_PANIC: return hal::ResetReason::PANIC;
    case ESP_RST_INT_WDT: return hal::ResetReason::INT_WATCHDOG;
    case ESP_RST_TASK_WDT: return hal::ResetReason::TASK_WATCHDOG;
    case ESP_RST_WDT: return hal::ResetReason::OTHER_WATCHDOG;
    case ESP_RST_DEEPSLEEP: return hal::ResetReason::DEEP_SLEEP;
    case ESP_RST_BROWNOUT: return hal::ResetReason::BROWNOUT;
    case ESP_RST_SDIO: return hal::ResetReason::SDIO;
    case ESP_RST_USB: return hal::ResetReason::USB;
    case ESP_RST_JTAG: return hal::ResetReason::JTAG;
    case ESP_RST_EFUSE: return hal::ResetReason::EFUSE;
    case ESP_RST_PWR_GLITCH: return hal::ResetReason::POWER_GLITCH;
    case ESP_RST_CPU_LOCKUP: return hal::ResetReason::CPU_LOCKUP;
    default: return hal::ResetReason::UNKNOWN;
    }
}

hal::TaskState map_state(eTaskState state)
{
    switch (state) {
    case eRunning: return hal::TaskState::RUNNING;
    case eReady: return hal::TaskState::READY;
    case eBlocked: return hal::TaskState::BLOCKED;
    case eSuspended: return hal::TaskState::SUSPENDED;
    case eDeleted: return hal::TaskState::DELETED;
    default: return hal::TaskState::UNKNOWN;
    }
}

void partition_kind(const esp_partition_t& part, char* p_out, size_t len)
{
    if (part.type == ESP_PARTITION_TYPE_APP) {
        if (part.subtype == ESP_PARTITION_SUBTYPE_APP_FACTORY) {
            copy_text(p_out, len, "app/factory");
        } else if ((part.subtype >= ESP_PARTITION_SUBTYPE_APP_OTA_MIN) &&
                   (part.subtype < ESP_PARTITION_SUBTYPE_APP_OTA_MAX)) {
            (void)std::snprintf(p_out, len, "app/ota_%d",
                                static_cast<int>(part.subtype - ESP_PARTITION_SUBTYPE_APP_OTA_MIN));
        } else {
            (void)std::snprintf(p_out, len, "app/0x%02x", static_cast<unsigned>(part.subtype));
        }
        return;
    }
    static const struct {
        esp_partition_subtype_t subtype;
        const char* p_name;
    } DATA[] = {{ESP_PARTITION_SUBTYPE_DATA_OTA, "data/ota"},         {ESP_PARTITION_SUBTYPE_DATA_PHY, "data/phy"},
                {ESP_PARTITION_SUBTYPE_DATA_NVS, "data/nvs"},         {ESP_PARTITION_SUBTYPE_DATA_COREDUMP, "data/coredump"},
                {ESP_PARTITION_SUBTYPE_DATA_NVS_KEYS, "data/nvs_keys"}, {ESP_PARTITION_SUBTYPE_DATA_FAT, "data/fat"},
                {ESP_PARTITION_SUBTYPE_DATA_SPIFFS, "data/spiffs"},   {ESP_PARTITION_SUBTYPE_DATA_LITTLEFS, "data/littlefs"}};
    if (part.type == ESP_PARTITION_TYPE_DATA) {
        for (const auto& entry : DATA) {
            if (entry.subtype == part.subtype) {
                copy_text(p_out, len, entry.p_name);
                return;
            }
        }
    }
    (void)std::snprintf(p_out, len, "0x%02x/0x%02x", static_cast<unsigned>(part.type),
                        static_cast<unsigned>(part.subtype));
}

uint8_t core_count()
{
    return (portNUM_PROCESSORS < hal::SYSTEM_MAX_CORES) ? static_cast<uint8_t>(portNUM_PROCESSORS)
                                                       : hal::SYSTEM_MAX_CORES;
}
} // namespace

Esp32System::Esp32System()
    : m_lock(portMUX_INITIALIZER_UNLOCKED), m_last_sample(0U), m_last_idle{}, m_load_pct{}, m_p_tsens(nullptr),
      m_tsens_failed(false), m_p_trace_buf(nullptr), m_trace_capacity(0U), m_trace_running(false)
{
    count_boot();
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

// ============================================================================
// Introspection
// ============================================================================
common::Status Esp32System::get_chip_details(hal::ChipDetails& details) const
{
    details = hal::ChipDetails{};
    esp_chip_info_t chip{};
    esp_chip_info(&chip);
    copy_text(details.model, sizeof(details.model), model_name(chip.model));
#if CONFIG_IDF_TARGET_ARCH_RISCV
    copy_text(details.arch, sizeof(details.arch), "RISC-V (RV32IMAFC)");
#else
    copy_text(details.arch, sizeof(details.arch), "Xtensa LX");
#endif
    details.revision = chip.revision;
    details.cores = chip.cores;
    details.cpu_mhz = static_cast<uint32_t>(CONFIG_ESP_DEFAULT_CPU_FREQ_MHZ);
    uint32_t flash = 0U;
    details.flash_bytes = (esp_flash_get_size(nullptr, &flash) == ESP_OK) ? flash : 0U;
    details.psram_bytes = esp_psram_is_initialized() ? static_cast<uint32_t>(esp_psram_get_size()) : 0U;
    details.has_wifi = (chip.features & CHIP_FEATURE_WIFI_BGN) != 0U;
    details.has_bluetooth = (chip.features & (CHIP_FEATURE_BT | CHIP_FEATURE_BLE)) != 0U;
    details.has_ieee802154 = (chip.features & CHIP_FEATURE_IEEE802154) != 0U;
    details.has_mac = (esp_efuse_mac_get_default(details.mac) == ESP_OK);
    return common::Status::OK;
}

common::Status Esp32System::get_firmware_info(hal::FirmwareInfo& info) const
{
    const esp_app_desc_t* const p_desc = esp_app_get_description();
    copy_text(info.project, sizeof(info.project), p_desc->project_name);
    copy_text(info.version, sizeof(info.version), p_desc->version);
    copy_text(info.build_date, sizeof(info.build_date), p_desc->date);
    copy_text(info.build_time, sizeof(info.build_time), p_desc->time);
    copy_text(info.sdk_version, sizeof(info.sdk_version), p_desc->idf_ver);
    return common::Status::OK;
}

common::Status Esp32System::get_boot_info(hal::BootInfo& info) const
{
    info.reason = map_reset(esp_reset_reason());
    info.boot_count = s_boot_count;
    return common::Status::OK;
}

common::Status Esp32System::get_tasks(hal::TaskInfo* p_out, size_t max, size_t& count, uint64_t& total_runtime) const
{
    count = 0U;
    total_runtime = 0U;
    if (p_out == nullptr) {
        return common::Status::ERR_PARAM;
    }
    configRUN_TIME_COUNTER_TYPE total = 0U;
    const UBaseType_t found = uxTaskGetSystemState(s_task_status, MAX_TASKS, &total);
    total_runtime = static_cast<uint64_t>(total);
    for (UBaseType_t index = 0U; (index < found) && (count < max); ++index) {
        const TaskStatus_t& src = s_task_status[index];
        hal::TaskInfo& dst = p_out[count++];
        copy_text(dst.name, sizeof(dst.name), src.pcTaskName);
        dst.id = static_cast<uint32_t>(src.xTaskNumber);
        dst.state = map_state(src.eCurrentState);
        dst.priority = static_cast<uint8_t>(src.uxCurrentPriority);
        const BaseType_t core = xTaskGetCoreID(src.xHandle);
        dst.core = (core == tskNO_AFFINITY) ? static_cast<int8_t>(-1) : static_cast<int8_t>(core);
        dst.stack_free_min = static_cast<uint32_t>(src.usStackHighWaterMark) * sizeof(StackType_t);
        dst.runtime = static_cast<uint64_t>(src.ulRunTimeCounter);
    }
    return common::Status::OK;
}

common::Status Esp32System::get_heap_regions(hal::HeapRegion* p_out, size_t max, size_t& count) const
{
    static const struct {
        const char* p_name;
        uint32_t caps;
    } REGIONS[] = {{"internal", MALLOC_CAP_INTERNAL | MALLOC_CAP_8BIT}, {"dma", MALLOC_CAP_DMA | MALLOC_CAP_8BIT},
                   {"psram", MALLOC_CAP_SPIRAM},
#ifdef MALLOC_CAP_EXEC
                   {"exec", MALLOC_CAP_EXEC},
#endif
                   {"rtc/lp", MALLOC_CAP_RTCRAM}};
    count = 0U;
    for (const auto& region : REGIONS) {
        if (count >= max) {
            break;
        }
        multi_heap_info_t info{};
        heap_caps_get_info(&info, region.caps);
        const size_t total = heap_caps_get_total_size(region.caps);
        if (total == 0U) {
            continue;
        }
        hal::HeapRegion& out = p_out[count++];
        copy_text(out.name, sizeof(out.name), region.p_name);
        out.total = static_cast<uint32_t>(total);
        out.free = static_cast<uint32_t>(info.total_free_bytes);
        out.min_free = static_cast<uint32_t>(info.minimum_free_bytes);
        out.largest = static_cast<uint32_t>(info.largest_free_block);
        out.allocated_blocks = static_cast<uint32_t>(info.allocated_blocks);
        out.free_blocks = static_cast<uint32_t>(info.free_blocks);
    }
    return common::Status::OK;
}

common::Status Esp32System::get_partitions(hal::FlashPartition* p_out, size_t max, size_t& count) const
{
    count = 0U;
    const esp_partition_t* const p_running = esp_ota_get_running_partition();
    esp_partition_iterator_t it = esp_partition_find(ESP_PARTITION_TYPE_ANY, ESP_PARTITION_SUBTYPE_ANY, nullptr);
    for (; (it != nullptr) && (count < max); it = esp_partition_next(it)) {
        const esp_partition_t* const p_part = esp_partition_get(it);
        hal::FlashPartition& out = p_out[count++];
        copy_text(out.label, sizeof(out.label), p_part->label);
        partition_kind(*p_part, out.kind, sizeof(out.kind));
        out.offset = p_part->address;
        out.size = p_part->size;
        out.is_running = (p_running != nullptr) && (p_running->address == p_part->address);
    }
    esp_partition_iterator_release(it);
    return common::Status::OK;
}

#if CONFIG_HEAP_TRACING_STANDALONE
common::Status Esp32System::heap_trace_start(size_t max_records)
{
    if (m_trace_running) {
        return common::Status::ERR_STATE;
    }
    const size_t records = ((max_records == 0U) || (max_records > TRACE_MAX_RECORDS)) ? TRACE_MAX_RECORDS : max_records;
    if ((m_p_trace_buf == nullptr) || (m_trace_capacity < records)) {
        heap_caps_free(m_p_trace_buf);
        m_p_trace_buf = heap_caps_calloc(records, sizeof(heap_trace_record_t), MALLOC_CAP_SPIRAM | MALLOC_CAP_8BIT);
        m_trace_capacity = (m_p_trace_buf != nullptr) ? records : 0U;
    }
    if (m_p_trace_buf == nullptr) {
        return common::Status::ERR_NO_MEM;
    }
    if ((::heap_trace_init_standalone(static_cast<heap_trace_record_t*>(m_p_trace_buf), records) != ESP_OK) ||
        (::heap_trace_start(HEAP_TRACE_LEAKS) != ESP_OK)) {
        return common::Status::ERR_IO;
    }
    m_trace_running = true;
    return common::Status::OK;
}

common::Status Esp32System::heap_trace_stop()
{
    if (!m_trace_running) {
        return common::Status::ERR_STATE;
    }
    m_trace_running = false;
    return (::heap_trace_stop() == ESP_OK) ? common::Status::OK : common::Status::ERR_IO;
}

common::Status Esp32System::heap_trace_status(size_t& count, size_t& bytes, bool& is_running) const
{
    count = 0U;
    bytes = 0U;
    is_running = m_trace_running;
    if (m_p_trace_buf == nullptr) {
        return common::Status::OK;
    }
    const size_t total = ::heap_trace_get_count();
    for (size_t index = 0U; index < total; ++index) {
        heap_trace_record_t record{};
        if ((::heap_trace_get(index, &record) == ESP_OK) && (record.address != nullptr) && !record.freed) {
            ++count;
            bytes += record.size;
        }
    }
    return common::Status::OK;
}

common::Status Esp32System::heap_trace_get(size_t index, hal::HeapTraceRecord& out) const
{
    // index đếm trên các khối còn sống (bỏ bản ghi rỗng / đã giải phóng).
    const size_t total = (m_p_trace_buf != nullptr) ? ::heap_trace_get_count() : 0U;
    size_t live = 0U;
    for (size_t raw = 0U; raw < total; ++raw) {
        heap_trace_record_t record{};
        if ((::heap_trace_get(raw, &record) != ESP_OK) || (record.address == nullptr) || record.freed) {
            continue;
        }
        if (live++ != index) {
            continue;
        }
        out = hal::HeapTraceRecord{};
        out.address = reinterpret_cast<uintptr_t>(record.address);
        out.size = static_cast<uint32_t>(record.size);
        out.core = static_cast<uint8_t>(record.ccount & 1U);
#if CONFIG_HEAP_TRACING_STACK_DEPTH > 0 // Nơi gọi chỉ có khi build bật frame pointer (RISC-V).
        for (size_t depth = 0U; (depth < hal::HEAP_TRACE_DEPTH) && (depth < CONFIG_HEAP_TRACING_STACK_DEPTH); ++depth) {
            out.callers[depth] = reinterpret_cast<uintptr_t>(record.alloced_by[depth]);
        }
#endif
        return common::Status::OK;
    }
    return common::Status::ERR_NOT_FOUND;
}
#else
common::Status Esp32System::heap_trace_start(size_t max_records)
{
    (void)max_records;
    return common::Status::ERR_NOT_SUPPORTED;
}

common::Status Esp32System::heap_trace_stop()
{
    return common::Status::ERR_NOT_SUPPORTED;
}

common::Status Esp32System::heap_trace_status(size_t& count, size_t& bytes, bool& is_running) const
{
    count = 0U;
    bytes = 0U;
    is_running = false;
    return common::Status::ERR_NOT_SUPPORTED;
}

common::Status Esp32System::heap_trace_get(size_t index, hal::HeapTraceRecord& record) const
{
    (void)index;
    (void)record;
    return common::Status::ERR_NOT_SUPPORTED;
}
#endif

} // namespace esp32_mcal
