/**
 * @file    settings_app.cpp
 * @brief   Hiện thực app cài đặt.
 */
#include "settings_app.hpp"

#include <cstdio>
#include <cstdlib>

#include "framework/app_manager.hpp"
#include "framework/services.hpp"
#include "framework/theme.hpp"
#include "ui_helpers.hpp"

namespace apps {

namespace {

constexpr fw::AppInfo INFO{"settings", "Settings", LV_SYMBOL_SETTINGS, 0x6E7681U, true};
constexpr int32_t BRIGHTNESS_MIN = 5; // Không cho tắt hẳn đèn nền -> tránh "màn hình chết".
constexpr int32_t BRIGHTNESS_MAX = 100;

// Các múi giờ đang dùng trên thế giới (phút so với UTC), gồm cả múi lẻ :30 / :45.
constexpr int16_t TIMEZONES_MIN[] = {
    -720, -660, -600, -570, -540, -480, -420, -360, -300, -240, -210, -180, -120, -60, 0,   60,  120, 180, 210,
    240,  270,  300,  330,  345,  360,  390,  420,  480,  525,  540,  570,  600,  630, 660, 720, 765, 780, 840};
constexpr size_t TIMEZONE_COUNT = sizeof(TIMEZONES_MIN) / sizeof(TIMEZONES_MIN[0]);
constexpr size_t TIMEZONE_LABEL_LEN = 10U; // "UTC+05:45\n"
constexpr int32_t MINUTES_PER_HOUR = 60;
constexpr int32_t TIMEZONE_DROPDOWN_WIDTH = 130;

/** @brief Dựng chuỗi tùy chọn dropdown "UTC-12:00\nUTC-11:00\n..." vào buffer tĩnh. */
const char* timezone_options()
{
    static char s_options[TIMEZONE_COUNT * TIMEZONE_LABEL_LEN + 1U] = {};
    if (s_options[0] == '\0') {
        size_t used = 0U;
        for (size_t index = 0U; index < TIMEZONE_COUNT; ++index) {
            const int32_t offset = TIMEZONES_MIN[index];
            const int32_t magnitude = std::abs(offset);
            const int written = std::snprintf(&s_options[used], sizeof(s_options) - used, "%sUTC%c%02d:%02d",
                                              (index == 0U) ? "" : "\n", (offset < 0) ? '-' : '+',
                                              static_cast<int>(magnitude / MINUTES_PER_HOUR),
                                              static_cast<int>(magnitude % MINUTES_PER_HOUR));
            if (written > 0) {
                used += static_cast<size_t>(written);
            }
        }
    }
    return s_options;
}

/** @return Chỉ số của offset trong bảng, hoặc chỉ số UTC+0 nếu không có. */
uint32_t timezone_index(int32_t offset_min)
{
    uint32_t utc_index = 0U;
    for (size_t index = 0U; index < TIMEZONE_COUNT; ++index) {
        if (TIMEZONES_MIN[index] == offset_min) {
            return static_cast<uint32_t>(index);
        }
        if (TIMEZONES_MIN[index] == 0) {
            utc_index = static_cast<uint32_t>(index);
        }
    }
    return utc_index;
}

} // namespace

SettingsApp::SettingsApp()
    : fw::App(INFO), m_p_wifi_status(nullptr), m_p_brightness_label(nullptr), m_p_timezone(nullptr), m_p_dialog(nullptr)
{
}

void SettingsApp::on_create(lv_obj_t* p_root)
{
    ui::column_layout(p_root);
    fw::Services& services = context().services;

    create_wifi_row(p_root);

    m_p_brightness_label = ui::label(p_root, "", fw::theme::font_small(), fw::theme::COLOR_TEXT);
    lv_obj_t* const p_slider = lv_slider_create(p_root);
    lv_obj_set_width(p_slider, LV_PCT(90));
    lv_slider_set_range(p_slider, BRIGHTNESS_MIN, BRIGHTNESS_MAX);
    lv_slider_set_value(p_slider, static_cast<int32_t>(services.brightness()), LV_ANIM_OFF);
    lv_obj_add_event_cb(p_slider, &SettingsApp::on_brightness_changed, LV_EVENT_VALUE_CHANGED, this);
    if (!services.has_backlight()) {
        lv_obj_add_state(p_slider, LV_STATE_DISABLED);
    }
    update_brightness_label(services.brightness());

    create_timezone_row(p_root);

    lv_obj_t* const p_restart = ui::button(p_root, LV_SYMBOL_REFRESH " Restart", &SettingsApp::on_restart_clicked, this);
    lv_obj_set_style_bg_color(p_restart, lv_color_hex(fw::theme::COLOR_DANGER), 0);
}

void SettingsApp::create_wifi_row(lv_obj_t* p_root)
{
    // Dòng kiểu điện thoại: "Wi-Fi      <trạng thái>  >" – chạm để mở trang Wi-Fi.
    lv_obj_t* const p_row = lv_button_create(p_root);
    lv_obj_set_size(p_row, LV_PCT(100), LV_SIZE_CONTENT);
    lv_obj_set_flex_flow(p_row, LV_FLEX_FLOW_ROW);
    lv_obj_set_flex_align(p_row, LV_FLEX_ALIGN_SPACE_BETWEEN, LV_FLEX_ALIGN_CENTER, LV_FLEX_ALIGN_CENTER);
    lv_obj_set_style_bg_color(p_row, lv_color_hex(fw::theme::COLOR_BAR_BG), 0);
    lv_obj_set_style_shadow_width(p_row, 0, 0);
    lv_obj_add_event_cb(p_row, &SettingsApp::on_wifi_clicked, LV_EVENT_CLICKED, this);

    (void)ui::label(p_row, LV_SYMBOL_WIFI " Wi-Fi", fw::theme::font_small(), fw::theme::COLOR_TEXT);
    m_p_wifi_status = ui::label(p_row, "", fw::theme::font_small(), fw::theme::COLOR_TEXT_MUTED);
    lv_obj_set_flex_grow(m_p_wifi_status, 1);
    lv_obj_set_style_text_align(m_p_wifi_status, LV_TEXT_ALIGN_RIGHT, 0);
    lv_label_set_long_mode(m_p_wifi_status, LV_LABEL_LONG_DOT);
    (void)ui::label(p_row, LV_SYMBOL_RIGHT, fw::theme::font_small(), fw::theme::COLOR_TEXT_MUTED);
}

void SettingsApp::on_resume()
{
    // Cập nhật trạng thái khi quay về từ trang Wi-Fi.
    const fw::WifiService& wifi = context().services.wifi();
    const char* p_status = "Off";
    switch (wifi.status()) {
    case fw::WifiStatus::UNAVAILABLE:
        p_status = "Not available";
        break;
    case fw::WifiStatus::CONNECTING:
        p_status = "Connecting...";
        break;
    case fw::WifiStatus::CONNECTED:
        p_status = wifi.current_ssid();
        break;
    case fw::WifiStatus::FAILED:
        p_status = "Failed";
        break;
    case fw::WifiStatus::DISCONNECTED:
    default:
        break;
    }
    lv_label_set_text(m_p_wifi_status, p_status);
}

void SettingsApp::on_wifi_clicked(lv_event_t* p_event)
{
    (void)static_cast<SettingsApp*>(lv_event_get_user_data(p_event))->context().apps.launch("wifi");
}

void SettingsApp::create_timezone_row(lv_obj_t* p_root)
{
    lv_obj_t* const p_row = lv_obj_create(p_root);
    lv_obj_remove_style_all(p_row);
    lv_obj_set_size(p_row, LV_PCT(100), LV_SIZE_CONTENT);
    lv_obj_set_flex_flow(p_row, LV_FLEX_FLOW_ROW);
    lv_obj_set_flex_align(p_row, LV_FLEX_ALIGN_SPACE_BETWEEN, LV_FLEX_ALIGN_CENTER, LV_FLEX_ALIGN_CENTER);

    (void)ui::label(p_row, LV_SYMBOL_GPS " Time zone", fw::theme::font_small(), fw::theme::COLOR_TEXT);
    m_p_timezone = lv_dropdown_create(p_row);
    lv_obj_set_width(m_p_timezone, TIMEZONE_DROPDOWN_WIDTH);
    lv_dropdown_set_options_static(m_p_timezone, timezone_options());
    lv_dropdown_set_selected(m_p_timezone, timezone_index(context().services.time().utc_offset_minutes()));
    lv_obj_add_event_cb(m_p_timezone, &SettingsApp::on_timezone_changed, LV_EVENT_VALUE_CHANGED, this);
}

void SettingsApp::on_pause()
{
    close_dialog(); // Dialog nằm trên top-layer, phải đóng khi app rời foreground.
    if ((m_p_timezone != nullptr) && lv_dropdown_is_open(m_p_timezone)) {
        lv_dropdown_close(m_p_timezone); // Danh sách dropdown cũng nằm ngoài cây p_root.
    }
}

void SettingsApp::on_destroy()
{
    m_p_timezone = nullptr;
}

bool SettingsApp::on_back()
{
    if (m_p_dialog != nullptr) {
        close_dialog();
        return true;
    }
    if ((m_p_timezone != nullptr) && lv_dropdown_is_open(m_p_timezone)) {
        lv_dropdown_close(m_p_timezone);
        return true;
    }
    return false;
}

void SettingsApp::update_brightness_label(uint8_t percent)
{
    lv_label_set_text_fmt(m_p_brightness_label, LV_SYMBOL_IMAGE " Brightness: %u%%", static_cast<unsigned>(percent));
}

void SettingsApp::close_dialog()
{
    if (m_p_dialog != nullptr) {
        lv_msgbox_close_async(m_p_dialog); // Async: có thể đang ở trong callback của chính dialog.
        m_p_dialog = nullptr;
    }
}

void SettingsApp::on_brightness_changed(lv_event_t* p_event)
{
    auto* const p_self = static_cast<SettingsApp*>(lv_event_get_user_data(p_event));
    const int32_t value = lv_slider_get_value(lv_event_get_target_obj(p_event));
    const auto percent = static_cast<uint8_t>(value);
    if (common::is_ok(p_self->context().services.set_brightness(percent))) {
        p_self->update_brightness_label(percent);
    }
}

void SettingsApp::on_timezone_changed(lv_event_t* p_event)
{
    auto* const p_self = static_cast<SettingsApp*>(lv_event_get_user_data(p_event));
    const uint32_t index = lv_dropdown_get_selected(lv_event_get_target_obj(p_event));
    if (index < TIMEZONE_COUNT) {
        // Lưu NVS qua TimeService; status bar cập nhật ở lần làm mới kế tiếp (<= 1 s).
        (void)p_self->context().services.time().set_utc_offset_minutes(TIMEZONES_MIN[index]);
    }
}

void SettingsApp::on_restart_clicked(lv_event_t* p_event)
{
    auto* const p_self = static_cast<SettingsApp*>(lv_event_get_user_data(p_event));
    if (p_self->m_p_dialog != nullptr) {
        return;
    }
    p_self->m_p_dialog = lv_msgbox_create(nullptr);
    lv_msgbox_add_title(p_self->m_p_dialog, "Restart device?");
    lv_obj_t* const p_ok = lv_msgbox_add_footer_button(p_self->m_p_dialog, "Restart");
    lv_obj_add_event_cb(p_ok, &SettingsApp::on_dialog_confirm, LV_EVENT_CLICKED, p_self);
    lv_obj_t* const p_cancel = lv_msgbox_add_footer_button(p_self->m_p_dialog, "Cancel");
    lv_obj_add_event_cb(p_cancel, &SettingsApp::on_dialog_cancel, LV_EVENT_CLICKED, p_self);
}

void SettingsApp::on_dialog_confirm(lv_event_t* p_event)
{
    static_cast<SettingsApp*>(lv_event_get_user_data(p_event))->context().services.restart();
}

void SettingsApp::on_dialog_cancel(lv_event_t* p_event)
{
    static_cast<SettingsApp*>(lv_event_get_user_data(p_event))->close_dialog();
}

} // namespace apps
