/**
 * @file    settings_app.cpp
 * @brief   Hiện thực app cài đặt.
 */
#include "settings_app.hpp"

#include "framework/services.hpp"
#include "framework/theme.hpp"
#include "ui_helpers.hpp"

namespace apps {

namespace {

constexpr fw::AppInfo INFO{"settings", "Settings", LV_SYMBOL_SETTINGS, 0x6E7681U, true};
constexpr int32_t BRIGHTNESS_MIN = 5; // Không cho tắt hẳn đèn nền -> tránh "màn hình chết".
constexpr int32_t BRIGHTNESS_MAX = 100;

} // namespace

SettingsApp::SettingsApp() : fw::App(INFO), m_p_brightness_label(nullptr), m_p_dialog(nullptr) {}

void SettingsApp::on_create(lv_obj_t* p_root)
{
    ui::column_layout(p_root);
    fw::Services& services = context().services;

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

    lv_obj_t* const p_restart = ui::button(p_root, LV_SYMBOL_REFRESH " Restart", &SettingsApp::on_restart_clicked, this);
    lv_obj_set_style_bg_color(p_restart, lv_color_hex(fw::theme::COLOR_DANGER), 0);
}

void SettingsApp::on_pause()
{
    close_dialog(); // Dialog nằm trên top-layer, phải đóng khi app rời foreground.
}

bool SettingsApp::on_back()
{
    if (m_p_dialog != nullptr) {
        close_dialog();
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
