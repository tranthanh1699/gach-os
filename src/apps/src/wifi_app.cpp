/**
 * @file    wifi_app.cpp
 * @brief   Hiện thực app Wi-Fi.
 */
#include "wifi_app.hpp"

#include <cstdio>

#include "common/string_utils.hpp"
#include "framework/list_view.hpp"
#include "framework/services.hpp"
#include "framework/system_ui.hpp"
#include "framework/theme.hpp"
#include "ui_helpers.hpp"

namespace apps {

namespace {

constexpr fw::AppInfo INFO{"wifi", "Wi-Fi", LV_SYMBOL_WIFI, 0x1F6FEBU, false}; // Mở từ Settings, không có icon home.
constexpr uint32_t REFRESH_PERIOD_MS = 500U;
constexpr int32_t HEADER_BUTTON_SIZE = 36;
constexpr size_t ITEM_TEXT_LEN = 64U;
constexpr size_t STATUS_TEXT_LEN = 80U;
// ---- Màn nhập mật khẩu (dp) – ô nhập ở giữa phần trên, bàn phím hệ thống (fw::keyboard) ở đáy ----
constexpr uint32_t COLOR_KEY_SPECIAL = fw::keyboard::COLOR_KEY_SPECIAL;
constexpr uint32_t COLOR_ACCENT = fw::palette::ACCENT;
constexpr int32_t KB_KEY_GAP = fw::keyboard::KEY_GAP;
constexpr int32_t FORM_GAP = 14;
constexpr int32_t FIELD_WIDTH = 420;
constexpr int32_t FIELD_PAD = fw::keyboard::FIELD_PAD;
constexpr int32_t ACTION_WIDTH = 120;
constexpr int32_t ACTION_HEIGHT = 40;
constexpr size_t TITLE_TEXT_LEN = 96U;

lv_obj_t* plain_row(lv_obj_t* p_parent, int32_t gap)
{
    lv_obj_t* const p_row = lv_obj_create(p_parent);
    lv_obj_remove_style_all(p_row);
    lv_obj_set_size(p_row, LV_SIZE_CONTENT, LV_SIZE_CONTENT);
    lv_obj_set_flex_flow(p_row, LV_FLEX_FLOW_ROW);
    lv_obj_set_flex_align(p_row, LV_FLEX_ALIGN_CENTER, LV_FLEX_ALIGN_CENTER, LV_FLEX_ALIGN_CENTER);
    lv_obj_set_style_pad_column(p_row, gap, 0);
    return p_row;
}

lv_obj_t* action_button(lv_obj_t* p_parent, const char* p_text, uint32_t bg, lv_event_cb_t cb, void* p_user)
{
    lv_obj_t* const p_btn = ui::button(p_parent, p_text, cb, p_user);
    lv_obj_set_size(p_btn, fw::theme::px(ACTION_WIDTH), fw::theme::px(ACTION_HEIGHT));
    lv_obj_set_style_bg_color(p_btn, lv_color_hex(bg), 0);
    lv_obj_set_style_radius(p_btn, LV_RADIUS_CIRCLE, 0);
    lv_obj_set_style_shadow_width(p_btn, 0, 0);
    return p_btn;
}

lv_obj_t* icon_button(lv_obj_t* p_parent, const char* p_symbol, lv_event_cb_t cb, void* p_user)
{
    lv_obj_t* const p_btn = ui::button(p_parent, p_symbol, cb, p_user);
    lv_obj_set_size(p_btn, fw::theme::px(HEADER_BUTTON_SIZE), fw::theme::px(HEADER_BUTTON_SIZE));
    return p_btn;
}

} // namespace

WifiApp::WifiApp()
    : fw::App(INFO), m_p_root(nullptr), m_p_status(nullptr), m_p_scan_btn(nullptr), m_p_disconnect_btn(nullptr),
      m_p_list(nullptr), m_p_dialog(nullptr), m_p_password(nullptr), m_p_timer(nullptr), m_shown_generation(0U),
      m_selected_ssid{}
{
}

void WifiApp::on_create(lv_obj_t* p_root)
{
    m_p_root = p_root;
    ui::column_layout(p_root);
    create_header(p_root);

    m_p_list = fw::list_view::create(p_root);

    // Ép dựng lại danh sách lần đầu dù generation đang là 0.
    m_shown_generation = context().services.wifi().scan_generation() + 1U;
}

void WifiApp::create_header(lv_obj_t* p_root)
{
    lv_obj_t* const p_header = lv_obj_create(p_root);
    lv_obj_remove_style_all(p_header);
    lv_obj_set_size(p_header, LV_PCT(100), LV_SIZE_CONTENT);
    lv_obj_set_flex_flow(p_header, LV_FLEX_FLOW_ROW);
    lv_obj_set_flex_align(p_header, LV_FLEX_ALIGN_START, LV_FLEX_ALIGN_CENTER, LV_FLEX_ALIGN_CENTER);
    lv_obj_set_style_pad_column(p_header, fw::theme::pad(), 0);

    m_p_status = ui::label(p_header, "", fw::theme::font_small(), fw::theme::COLOR_TEXT);
    lv_obj_set_flex_grow(m_p_status, 1);
    lv_label_set_long_mode(m_p_status, LV_LABEL_LONG_DOT);

    m_p_scan_btn = icon_button(p_header, LV_SYMBOL_REFRESH, &WifiApp::on_scan_clicked, this);
    m_p_disconnect_btn = icon_button(p_header, LV_SYMBOL_CLOSE, &WifiApp::on_disconnect_clicked, this);
    lv_obj_set_style_bg_color(m_p_disconnect_btn, lv_color_hex(fw::theme::COLOR_DANGER), 0);
}

void WifiApp::on_resume()
{
    fw::WifiService& wifi = context().services.wifi();
    if ((wifi.network_count() == 0U) && !wifi.is_scanning()) {
        (void)wifi.scan();
    }
    refresh();
    m_p_timer = lv_timer_create(&WifiApp::on_timer, REFRESH_PERIOD_MS, this);
}

void WifiApp::on_pause()
{
    close_password_dialog();
    if (m_p_timer != nullptr) {
        lv_timer_delete(m_p_timer);
        m_p_timer = nullptr;
    }
}

bool WifiApp::on_back()
{
    if (m_p_dialog != nullptr) {
        close_password_dialog();
        return true;
    }
    return false;
}

void WifiApp::refresh()
{
    const fw::WifiService& wifi = context().services.wifi();
    refresh_status_label();

    const bool connected = (wifi.status() == fw::WifiStatus::CONNECTED) ||
                           (wifi.status() == fw::WifiStatus::CONNECTING);
    if (connected) {
        lv_obj_set_hidden(m_p_disconnect_btn, false);
    } else {
        lv_obj_set_hidden(m_p_disconnect_btn, true);
    }
    if (wifi.is_scanning() || !wifi.is_available()) {
        lv_obj_add_state(m_p_scan_btn, LV_STATE_DISABLED);
    } else {
        lv_obj_remove_state(m_p_scan_btn, LV_STATE_DISABLED);
    }

    if (wifi.scan_generation() != m_shown_generation) {
        m_shown_generation = wifi.scan_generation();
        rebuild_list();
    }
}

void WifiApp::refresh_status_label()
{
    const fw::WifiService& wifi = context().services.wifi();
    char text[STATUS_TEXT_LEN] = {};
    char ip[fw::IP_ADDRESS_BUF_LEN] = {};

    switch (wifi.status()) {
    case fw::WifiStatus::UNAVAILABLE:
        (void)common::copy_string(text, sizeof(text), "Wi-Fi not available");
        break;
    case fw::WifiStatus::CONNECTING:
        (void)std::snprintf(text, sizeof(text), "Connecting %s...", wifi.current_ssid());
        break;
    case fw::WifiStatus::CONNECTED:
        (void)wifi.ip_address(ip, sizeof(ip));
        (void)std::snprintf(text, sizeof(text), LV_SYMBOL_OK " %s  %s", wifi.current_ssid(), ip);
        break;
    case fw::WifiStatus::FAILED:
        (void)std::snprintf(text, sizeof(text), LV_SYMBOL_WARNING " Failed: %s", wifi.current_ssid());
        break;
    case fw::WifiStatus::DISCONNECTED:
    default:
        (void)common::copy_string(text, sizeof(text), wifi.is_scanning() ? "Scanning..." : "Not connected");
        break;
    }
    lv_label_set_text(m_p_status, text);
}

void WifiApp::rebuild_list()
{
    const fw::WifiService& wifi = context().services.wifi();
    lv_obj_clean(m_p_list);

    for (size_t index = 0U; index < wifi.network_count(); ++index) {
        const fw::WifiNetwork* const p_net = wifi.network_at(index);
        char text[ITEM_TEXT_LEN] = {};
        (void)std::snprintf(text, sizeof(text), "%s  %ddBm%s", p_net->ssid, static_cast<int>(p_net->rssi),
                            p_net->secure ? "" : "  (open)");
        lv_obj_t* const p_item = fw::list_view::add(m_p_list, LV_SYMBOL_WIFI, text);
        // Lưu chỉ số mạng trong user data (không cấp phát).
        lv_obj_set_user_data(p_item, reinterpret_cast<void*>(static_cast<uintptr_t>(index)));
        lv_obj_add_event_cb(p_item, &WifiApp::on_network_clicked, LV_EVENT_CLICKED, this);
    }
}

void WifiApp::select_network(size_t index)
{
    const fw::WifiNetwork* const p_net = context().services.wifi().network_at(index);
    if (p_net == nullptr) {
        return;
    }
    (void)common::copy_string(m_selected_ssid, sizeof(m_selected_ssid), p_net->ssid);
    if (p_net->secure) {
        open_password_dialog();
    } else {
        connect_selected("");
    }
}

void WifiApp::open_password_dialog()
{
    if (m_p_dialog != nullptr) {
        return;
    }
    // Màn nổi phủ toàn vùng app (FLOATING: không tham gia layout flex của root).
    // Phần trên (co giãn): tiêu đề, ô mật khẩu + nút hiện/ẩn, Cancel/Join. Phần dưới: bàn phím cao cố định.
    m_p_dialog = lv_obj_create(m_p_root);
    lv_obj_remove_style_all(m_p_dialog);
    lv_obj_set_floating(m_p_dialog, true);
    lv_obj_set_size(m_p_dialog, LV_PCT(100), LV_PCT(100));
    lv_obj_align(m_p_dialog, LV_ALIGN_TOP_LEFT, 0, 0);
    lv_obj_set_style_bg_color(m_p_dialog, lv_color_hex(fw::theme::COLOR_SCREEN_BG), 0);
    lv_obj_set_style_bg_opa(m_p_dialog, LV_OPA_COVER, 0);
    lv_obj_set_flex_flow(m_p_dialog, LV_FLEX_FLOW_COLUMN);
    lv_obj_set_scrollable(m_p_dialog, false);
    lv_obj_set_clickable(m_p_dialog, true); // Chặn chạm xuống danh sách mạng phía sau.

    lv_obj_t* const p_form = lv_obj_create(m_p_dialog);
    lv_obj_remove_style_all(p_form);
    lv_obj_set_width(p_form, LV_PCT(100));
    lv_obj_set_flex_grow(p_form, 1);
    lv_obj_set_flex_flow(p_form, LV_FLEX_FLOW_COLUMN);
    lv_obj_set_flex_align(p_form, LV_FLEX_ALIGN_CENTER, LV_FLEX_ALIGN_CENTER, LV_FLEX_ALIGN_CENTER);
    lv_obj_set_style_pad_row(p_form, fw::theme::px(FORM_GAP), 0);

    char title[TITLE_TEXT_LEN] = {};
    (void)std::snprintf(title, sizeof(title), "Enter the password for \"%s\"", m_selected_ssid);
    (void)ui::label(p_form, title, fw::theme::font_medium(), fw::theme::COLOR_TEXT);

    lv_obj_t* const p_field_row = plain_row(p_form, fw::theme::px(KB_KEY_GAP));
    m_p_password = lv_textarea_create(p_field_row);
    fw::keyboard::style_field(m_p_password);
    lv_obj_set_width(m_p_password, fw::theme::px(FIELD_WIDTH));
    lv_textarea_set_one_line(m_p_password, true);
    lv_textarea_set_password_mode(m_p_password, true);
    lv_textarea_set_max_length(m_p_password, static_cast<uint32_t>(fw::WIFI_PASSWORD_BUF_LEN - 1U));
    lv_textarea_set_placeholder_text(m_p_password, "Password");
    lv_obj_add_state(m_p_password, LV_STATE_FOCUSED); // Hiện con trỏ ngay.
    lv_obj_t* const p_reveal = action_button(p_field_row, LV_SYMBOL_EYE_OPEN, COLOR_KEY_SPECIAL,
                                             &WifiApp::on_reveal_clicked, this);
    lv_obj_set_width(p_reveal, fw::theme::px(ACTION_HEIGHT + FIELD_PAD));

    lv_obj_t* const p_actions = plain_row(p_form, fw::theme::px(FORM_GAP));
    (void)action_button(p_actions, "Cancel", COLOR_KEY_SPECIAL, &WifiApp::on_cancel_clicked, this);
    (void)action_button(p_actions, "Join", COLOR_ACCENT, &WifiApp::on_join_clicked, this);

    lv_obj_t* const p_keyboard = lv_keyboard_create(m_p_dialog);
    lv_obj_set_size(p_keyboard, LV_PCT(100), fw::theme::px(fw::keyboard::HEIGHT));
    fw::keyboard::style(p_keyboard);
    lv_keyboard_set_textarea(p_keyboard, m_p_password);
    lv_obj_add_event_cb(p_keyboard, &WifiApp::on_keyboard_event, LV_EVENT_READY, this);
    lv_obj_add_event_cb(p_keyboard, &WifiApp::on_keyboard_event, LV_EVENT_CANCEL, this);
}

void WifiApp::close_password_dialog()
{
    if (m_p_dialog != nullptr) {
        // Async: có thể đang ở trong callback của chính bàn phím.
        lv_obj_delete_async(m_p_dialog);
        m_p_dialog = nullptr;
        m_p_password = nullptr;
    }
}

void WifiApp::connect_selected(const char* p_password)
{
    (void)context().services.wifi().connect(m_selected_ssid, p_password, true);
    refresh();
}

void WifiApp::on_timer(lv_timer_t* p_timer)
{
    static_cast<WifiApp*>(lv_timer_get_user_data(p_timer))->refresh();
}

void WifiApp::on_scan_clicked(lv_event_t* p_event)
{
    auto* const p_self = static_cast<WifiApp*>(lv_event_get_user_data(p_event));
    (void)p_self->context().services.wifi().scan();
    p_self->refresh();
}

void WifiApp::on_disconnect_clicked(lv_event_t* p_event)
{
    auto* const p_self = static_cast<WifiApp*>(lv_event_get_user_data(p_event));
    (void)p_self->context().services.wifi().disconnect(true); // Ngắt chủ động = quên mạng.
    p_self->refresh();
}

void WifiApp::on_network_clicked(lv_event_t* p_event)
{
    auto* const p_self = static_cast<WifiApp*>(lv_event_get_user_data(p_event));
    void* const p_data = lv_obj_get_user_data(lv_event_get_current_target_obj(p_event));
    p_self->select_network(static_cast<size_t>(reinterpret_cast<uintptr_t>(p_data)));
}

void WifiApp::on_join_clicked(lv_event_t* p_event)
{
    auto* const p_self = static_cast<WifiApp*>(lv_event_get_user_data(p_event));
    if (p_self->m_p_password != nullptr) {
        p_self->connect_selected(lv_textarea_get_text(p_self->m_p_password));
    }
    p_self->close_password_dialog();
}

void WifiApp::on_cancel_clicked(lv_event_t* p_event)
{
    static_cast<WifiApp*>(lv_event_get_user_data(p_event))->close_password_dialog();
}

void WifiApp::on_reveal_clicked(lv_event_t* p_event)
{
    auto* const p_self = static_cast<WifiApp*>(lv_event_get_user_data(p_event));
    if (p_self->m_p_password == nullptr) {
        return;
    }
    const bool is_hidden = lv_textarea_get_password_mode(p_self->m_p_password);
    lv_textarea_set_password_mode(p_self->m_p_password, !is_hidden);
    lv_obj_t* const p_label = lv_obj_get_child(lv_event_get_current_target_obj(p_event), 0);
    if (p_label != nullptr) {
        lv_label_set_text_static(p_label, is_hidden ? LV_SYMBOL_EYE_CLOSE : LV_SYMBOL_EYE_OPEN);
    }
}

void WifiApp::on_keyboard_event(lv_event_t* p_event)
{
    auto* const p_self = static_cast<WifiApp*>(lv_event_get_user_data(p_event));
    if ((lv_event_get_code(p_event) == LV_EVENT_READY) && (p_self->m_p_password != nullptr)) {
        p_self->connect_selected(lv_textarea_get_text(p_self->m_p_password));
    }
    p_self->close_password_dialog();
}

} // namespace apps
