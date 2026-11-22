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
