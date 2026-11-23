/**
 * @file    services.cpp
 * @brief   Hiện thực facade dịch vụ hệ thống.
 */
#include "framework/services.hpp"

#include "hal_api/board.hpp"
#include "osal/osal.hpp"

namespace fw {

void Services::bind(const hal::Devices& devices)
{
    m_p_devices = &devices;
    m_wifi.bind(devices);
    m_time.bind(devices);
    m_files.bind(devices);
}

bool Services::has_backlight() const
{
    return (m_p_devices != nullptr) && (m_p_devices->p_backlight != nullptr);
}

uint8_t Services::brightness() const
{
    return has_backlight() ? m_p_devices->p_backlight->brightness() : 0U;
}

common::Status Services::set_brightness(uint8_t percent)
{
    if (!has_backlight()) {
        return common::Status::ERR_NOT_SUPPORTED;
    }
    return m_p_devices->p_backlight->set_brightness(percent);
}

void Services::device_info(DeviceInfo& info) const
{
    info = DeviceInfo{};
    if (m_p_devices == nullptr) {
        return;
    }
    hal::SystemInfo sys{};
    if (m_p_devices->p_system != nullptr) {
        m_p_devices->p_system->get_info(sys);
    }
    info.p_board_name = m_p_devices->p_board_name;
    info.p_chip_name = sys.p_chip_name;
    info.cpu_freq_mhz = sys.cpu_freq_mhz;
    info.cpu_cores = sys.cpu_cores;
    info.flash_size_kb = sys.flash_size_kb;
    info.heap_free_bytes = sys.heap_free_bytes;
    info.heap_min_bytes = sys.heap_min_bytes;
    if (m_p_devices->p_display != nullptr) {
        info.screen_width = m_p_devices->p_display->width();
        info.screen_height = m_p_devices->p_display->height();
    }
    info.has_touch = (m_p_devices->p_touch != nullptr);
}

namespace {

void fill_memory(hal::ISystem& system, SystemStats& stats)
{
    hal::MemoryStats mem{};
    stats.has_memory = (system.get_memory_stats(mem) == common::Status::OK);
    if (!stats.has_memory) {
        return;
    }
    stats.ram_total = mem.internal_total;
    stats.ram_free = mem.internal_free;
    stats.ram_min = mem.internal_min;
    stats.ram_largest = mem.internal_largest;
    stats.psram_total = mem.psram_total;
    stats.psram_free = mem.psram_free;
    stats.psram_min = mem.psram_min;
}

void fill_cpu(hal::ISystem& system, SystemStats& stats)
{
    hal::SystemInfo info{};
    system.get_info(info);
    stats.cpu_freq_mhz = info.cpu_freq_mhz;
    stats.cpu_cores = info.cpu_cores;

    hal::CpuStats cpu{};
    stats.has_cpu = (system.get_cpu_stats(cpu) == common::Status::OK);
    if (!stats.has_cpu) {
        return;
    }
    stats.cpu_cores = (cpu.core_count < STATS_MAX_CORES) ? cpu.core_count : STATS_MAX_CORES;
    for (uint8_t core = 0U; core < stats.cpu_cores; ++core) {
        stats.cpu_load_pct[core] = cpu.load_pct[core];
    }
    stats.task_count = cpu.task_count;
}

void fill_firmware(hal::ISystem& system, SystemStats& stats)
{
    hal::FirmwareStats fw_stats{};
    stats.has_firmware = (system.get_firmware_stats(fw_stats) == common::Status::OK);
    stats.flash_total = fw_stats.flash_total;
    stats.app_partition = fw_stats.app_partition;
    stats.app_used = fw_stats.app_used;
}

} // namespace

void Services::system_stats(SystemStats& stats) const
{
    stats = SystemStats{};
    if ((m_p_devices == nullptr) || (m_p_devices->p_system == nullptr)) {
        return;
    }
    hal::ISystem& system = *m_p_devices->p_system;
    fill_memory(system, stats);
    fill_cpu(system, stats);
    fill_firmware(system, stats);
    stats.has_temperature = (system.read_temperature(stats.temperature_c) == common::Status::OK);

    hal::IFileSystem* const p_fs = m_p_devices->p_fs;
    if ((p_fs != nullptr) && p_fs->is_mounted()) {
        stats.has_sd = (p_fs->get_capacity(stats.sd_total, stats.sd_free) == common::Status::OK);
    }
    if (m_p_devices->p_wifi != nullptr) {
        stats.has_wifi_rssi = m_p_devices->p_wifi->link_rssi(stats.wifi_rssi);
    }
}

uint32_t Services::uptime_ms() const
{
    return osal::tick_ms();
}

void Services::restart()
{
    if ((m_p_devices != nullptr) && (m_p_devices->p_system != nullptr)) {
        m_p_devices->p_system->restart();
    }
}

} // namespace fw
