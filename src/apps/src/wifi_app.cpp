/**
 * @file    wifi_app.cpp
 * @brief   Hiện thực app Wi-Fi.
 */
#include "wifi_app.hpp"

#include <cstdio>

#include "common/string_utils.hpp"
#include "framework/services.hpp"
#include "framework/theme.hpp"
#include "ui_helpers.hpp"

namespace apps {

namespace {

constexpr fw::AppInfo INFO{"wifi", "Wi-Fi", LV_SYMBOL_WIFI, 0x1F6FEBU, false}; // Mở từ Settings, không có icon home.
constexpr uint32_t REFRESH_PERIOD_MS = 500U;
constexpr int32_t HEADER_BUTTON_SIZE = 36;
constexpr size_t ITEM_TEXT_LEN = 64U;
constexpr size_t STATUS_TEXT_LEN = 80U;
constexpr int32_t DIALOG_PAD = 2;

lv_obj_t* icon_button(lv_obj_t* p_parent, const char* p_symbol, lv_event_cb_t cb, void* p_user)
{
    lv_obj_t* const p_btn = ui::button(p_parent, p_symbol, cb, p_user);
    lv_obj_set_size(p_btn, HEADER_BUTTON_SIZE, HEADER_BUTTON_SIZE);
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

    m_p_list = lv_list_create(p_root);
    lv_obj_set_width(m_p_list, LV_PCT(100));
    lv_obj_set_flex_grow(m_p_list, 1);

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
    lv_obj_set_style_pad_column(p_header, fw::theme::PAD_NORMAL, 0);

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
        lv_obj_remove_flag(m_p_disconnect_btn, LV_OBJ_FLAG_HIDDEN);
    } else {
        lv_obj_add_flag(m_p_disconnect_btn, LV_OBJ_FLAG_HIDDEN);
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
        lv_obj_t* const p_item = lv_list_add_button(m_p_list, LV_SYMBOL_WIFI, text);
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
    // Dialog nổi phủ toàn vùng app (FLOATING: không tham gia layout flex của root).
    m_p_dialog = lv_obj_create(m_p_root);
    lv_obj_add_flag(m_p_dialog, LV_OBJ_FLAG_FLOATING);
    lv_obj_set_size(m_p_dialog, LV_PCT(100), LV_PCT(100));
    lv_obj_align(m_p_dialog, LV_ALIGN_TOP_LEFT, 0, 0);
    lv_obj_set_style_pad_all(m_p_dialog, DIALOG_PAD, 0);
    lv_obj_set_style_pad_row(m_p_dialog, DIALOG_PAD, 0);
    lv_obj_set_style_radius(m_p_dialog, 0, 0);
    lv_obj_set_flex_flow(m_p_dialog, LV_FLEX_FLOW_COLUMN);
    lv_obj_remove_flag(m_p_dialog, LV_OBJ_FLAG_SCROLLABLE);

    m_p_password = lv_textarea_create(m_p_dialog);
    lv_obj_set_width(m_p_password, LV_PCT(100));
    lv_textarea_set_one_line(m_p_password, true);
    lv_textarea_set_password_mode(m_p_password, true);
    lv_textarea_set_max_length(m_p_password, static_cast<uint32_t>(fw::WIFI_PASSWORD_BUF_LEN - 1U));
    lv_textarea_set_placeholder_text(m_p_password, m_selected_ssid);

    lv_obj_t* const p_keyboard = lv_keyboard_create(m_p_dialog);
    lv_obj_set_width(p_keyboard, LV_PCT(100));
    lv_obj_set_flex_grow(p_keyboard, 1);
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

void WifiApp::on_keyboard_event(lv_event_t* p_event)
{
    auto* const p_self = static_cast<WifiApp*>(lv_event_get_user_data(p_event));
    if ((lv_event_get_code(p_event) == LV_EVENT_READY) && (p_self->m_p_password != nullptr)) {
        p_self->connect_selected(lv_textarea_get_text(p_self->m_p_password));
    }
    p_self->close_password_dialog();
}

} // namespace apps
