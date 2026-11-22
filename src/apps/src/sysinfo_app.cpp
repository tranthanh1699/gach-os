/**
 * @file    sysinfo_app.cpp
 * @brief   Hiện thực app thông tin hệ thống.
 */
#include "sysinfo_app.hpp"

#include <cstdio>

#include "common/string_utils.hpp"
#include "framework/services.hpp"
#include "framework/theme.hpp"
#include "ui_helpers.hpp"

namespace apps {

namespace {

constexpr fw::AppInfo INFO{"sysinfo", "System", LV_SYMBOL_LIST, 0x8957E5U, true};
constexpr uint32_t REFRESH_PERIOD_MS = 1000U;
constexpr uint32_t MS_PER_SECOND = 1000U;
constexpr uint32_t BYTES_PER_KIB = 1024U;
constexpr size_t NET_TEXT_LEN = 56U;
constexpr size_t CLOCK_TEXT_LEN = 32U; // Đủ cho trường hợp xấu nhất GCC tính (uint16/uint8 tối đa).

const char* or_unknown(const char* p_text)
{
    return (p_text != nullptr) ? p_text : "?";
}

} // namespace

SysInfoApp::SysInfoApp() : fw::App(INFO), m_p_text(nullptr), m_p_timer(nullptr) {}

void SysInfoApp::on_create(lv_obj_t* p_root)
{
    ui::column_layout(p_root);
    lv_obj_set_scroll_dir(p_root, LV_DIR_VER);
    m_p_text = ui::label(p_root, "", fw::theme::font_small(), fw::theme::COLOR_TEXT);
}

void SysInfoApp::on_resume()
{
    refresh();
    m_p_timer = lv_timer_create(&SysInfoApp::on_timer, REFRESH_PERIOD_MS, this);
}

void SysInfoApp::on_pause()
{
    if (m_p_timer != nullptr) {
        lv_timer_delete(m_p_timer);
        m_p_timer = nullptr;
    }
}

void SysInfoApp::refresh()
{
    fw::Services& services = context().services;
    fw::DeviceInfo info{};
    services.device_info(info);
    const uint32_t uptime_s = services.uptime_ms() / MS_PER_SECOND;

    char net[NET_TEXT_LEN] = {};
    char ip[fw::IP_ADDRESS_BUF_LEN] = {};
    if (services.wifi().status() == fw::WifiStatus::CONNECTED) {
        (void)services.wifi().ip_address(ip, sizeof(ip));
        (void)std::snprintf(net, sizeof(net), "%s (%s)", services.wifi().current_ssid(), ip);
    } else {
        (void)common::copy_string(net, sizeof(net), "offline");
    }

    char clock[CLOCK_TEXT_LEN] = {};
    fw::DateTime now{};
    if (services.time().now(now)) {
        (void)std::snprintf(clock, sizeof(clock), "%04u-%02u-%02u %02u:%02u:%02u", static_cast<unsigned>(now.year),
                            static_cast<unsigned>(now.month), static_cast<unsigned>(now.day),
                            static_cast<unsigned>(now.hour), static_cast<unsigned>(now.minute),
                            static_cast<unsigned>(now.second));
    } else {
        (void)common::copy_string(clock, sizeof(clock), "not synced");
    }

    lv_label_set_text_fmt(m_p_text,
                          "Board : %s\n"
                          "Chip  : %s x%u @ %u MHz\n"
                          "Flash : %u KiB\n"
                          "Heap  : %u KiB free (min %u)\n"
                          "Screen: %ux%u %s\n"
                          "Wi-Fi : %s\n"
                          "Time  : %s\n"
                          "LVGL  : %d.%d.%d\n"
                          "Uptime: %u s",
                          or_unknown(info.p_board_name), or_unknown(info.p_chip_name),
                          static_cast<unsigned>(info.cpu_cores), static_cast<unsigned>(info.cpu_freq_mhz),
                          static_cast<unsigned>(info.flash_size_kb),
                          static_cast<unsigned>(info.heap_free_bytes / BYTES_PER_KIB),
                          static_cast<unsigned>(info.heap_min_bytes / BYTES_PER_KIB),
                          static_cast<unsigned>(info.screen_width), static_cast<unsigned>(info.screen_height),
                          info.has_touch ? "touch" : "", net, clock, LVGL_VERSION_MAJOR, LVGL_VERSION_MINOR,
                          LVGL_VERSION_PATCH, static_cast<unsigned>(uptime_s));
}

void SysInfoApp::on_timer(lv_timer_t* p_timer)
{
    static_cast<SysInfoApp*>(lv_timer_get_user_data(p_timer))->refresh();
}

} // namespace apps
