/**
 * @file    shell.cpp
 * @brief   Hiện thực System UI (status bar + nav bar).
 */
#include "shell.hpp"

#include "framework/config.hpp"
#include "framework/theme.hpp"

namespace fw {

namespace {

// Kích thước theo dp (theme::px đổi sang pixel thật).
constexpr int32_t BAR_PAD_HOR = 8;
constexpr int32_t STATUS_ICON_GAP = 6;
constexpr int32_t NAV_BUTTON_WIDTH = 72;
constexpr int32_t NAV_BUTTON_INSET = 6; /**< Nút nav thấp hơn thanh nav một chút. */

lv_obj_t* create_bar(lv_obj_t* p_parent, int32_t height)
{
    lv_obj_t* const p_bar = lv_obj_create(p_parent);
    lv_obj_remove_style_all(p_bar);
    lv_obj_set_size(p_bar, LV_PCT(100), height);
    lv_obj_set_style_bg_color(p_bar, lv_color_hex(theme::COLOR_BAR_BG), 0);
    lv_obj_set_style_bg_opa(p_bar, LV_OPA_COVER, 0);
    lv_obj_set_style_pad_hor(p_bar, theme::px(BAR_PAD_HOR), 0);
    lv_obj_set_flex_flow(p_bar, LV_FLEX_FLOW_ROW);
    lv_obj_set_flex_align(p_bar, LV_FLEX_ALIGN_SPACE_BETWEEN, LV_FLEX_ALIGN_CENTER, LV_FLEX_ALIGN_CENTER);
    lv_obj_set_scrollable(p_bar, false);
    return p_bar;
}

} // namespace

Shell::Shell()
    : m_p_apps(nullptr), m_p_services(nullptr), m_p_title(nullptr), m_p_wifi_icon(nullptr), m_p_clock(nullptr),
      m_p_content(nullptr)
{
}

void Shell::create(AppManager& apps, const Services& services)
{
    m_p_apps = &apps;
    m_p_services = &services;

    lv_obj_t* const p_screen = lv_screen_active();
    lv_obj_set_style_bg_color(p_screen, lv_color_hex(theme::COLOR_SCREEN_BG), 0);
    lv_obj_set_style_pad_all(p_screen, 0, 0);
    lv_obj_set_style_pad_gap(p_screen, 0, 0);
    lv_obj_set_flex_flow(p_screen, LV_FLEX_FLOW_COLUMN);
    lv_obj_set_scrollable(p_screen, false);

    // Status bar: tiêu đề app (trái) | icon Wi-Fi + đồng hồ (phải).
    lv_obj_t* const p_status = create_bar(p_screen, theme::px(config::STATUS_BAR_HEIGHT));
    m_p_title = lv_label_create(p_status);
    lv_obj_set_style_text_color(m_p_title, lv_color_hex(theme::COLOR_TEXT), 0);
    lv_label_set_text(m_p_title, "");

    lv_obj_t* const p_right = lv_obj_create(p_status);
    lv_obj_remove_style_all(p_right);
    lv_obj_set_size(p_right, LV_SIZE_CONTENT, LV_SIZE_CONTENT);
    lv_obj_set_flex_flow(p_right, LV_FLEX_FLOW_ROW);
    lv_obj_set_flex_align(p_right, LV_FLEX_ALIGN_END, LV_FLEX_ALIGN_CENTER, LV_FLEX_ALIGN_CENTER);
    lv_obj_set_style_pad_column(p_right, theme::px(STATUS_ICON_GAP), 0);
    m_p_wifi_icon = lv_label_create(p_right);
    lv_label_set_text(m_p_wifi_icon, LV_SYMBOL_WIFI);
    m_p_clock = lv_label_create(p_right);
    lv_obj_set_style_text_color(m_p_clock, lv_color_hex(theme::COLOR_TEXT), 0);

    // Content area: chiếm toàn bộ phần còn lại.
    m_p_content = lv_obj_create(p_screen);
    lv_obj_remove_style_all(m_p_content);
    lv_obj_set_width(m_p_content, LV_PCT(100));
    lv_obj_set_flex_grow(m_p_content, 1);
    lv_obj_set_scrollable(m_p_content, false);

    // Nav bar: Back / Home (kiểu Android, tương đương soft-key của S60).
    lv_obj_t* const p_nav = create_bar(p_screen, theme::px(config::NAV_BAR_HEIGHT));
    lv_obj_set_flex_align(p_nav, LV_FLEX_ALIGN_SPACE_EVENLY, LV_FLEX_ALIGN_CENTER, LV_FLEX_ALIGN_CENTER);
    (void)create_nav_button(p_nav, LV_SYMBOL_LEFT, &Shell::on_back_clicked);
    (void)create_nav_button(p_nav, LV_SYMBOL_HOME, &Shell::on_home_clicked);

    (void)lv_timer_create(&Shell::on_status_timer, config::STATUS_REFRESH_MS, this);
    refresh_status();
}

void Shell::on_foreground_changed(const App& app)
{
    lv_label_set_text(m_p_title, app.info().p_name);
}

lv_obj_t* Shell::create_nav_button(lv_obj_t* p_parent, const char* p_symbol, lv_event_cb_t cb)
{
    lv_obj_t* const p_btn = lv_button_create(p_parent);
    lv_obj_set_size(p_btn, theme::px(NAV_BUTTON_WIDTH), theme::px(config::NAV_BAR_HEIGHT - NAV_BUTTON_INSET));
    lv_obj_set_style_bg_opa(p_btn, LV_OPA_TRANSP, 0);
    lv_obj_set_style_shadow_width(p_btn, 0, 0);
    lv_obj_add_event_cb(p_btn, cb, LV_EVENT_CLICKED, this);

    lv_obj_t* const p_label = lv_label_create(p_btn);
    lv_label_set_text(p_label, p_symbol);
    lv_obj_set_style_text_color(p_label, lv_color_hex(theme::COLOR_TEXT), 0);
    lv_obj_center(p_label);
    return p_btn;
}

void Shell::on_back_clicked(lv_event_t* p_event)
{
    static_cast<Shell*>(lv_event_get_user_data(p_event))->m_p_apps->back();
}

void Shell::on_home_clicked(lv_event_t* p_event)
{
    static_cast<Shell*>(lv_event_get_user_data(p_event))->m_p_apps->home();
}

void Shell::on_status_timer(lv_timer_t* p_timer)
{
    static_cast<Shell*>(lv_timer_get_user_data(p_timer))->refresh_status();
}

void Shell::refresh_status()
{
    DateTime now{};
    if (m_p_services->time().now(now)) {
        lv_label_set_text_fmt(m_p_clock, "%02u:%02u", static_cast<unsigned>(now.hour), static_cast<unsigned>(now.minute));
    } else {
        lv_label_set_text(m_p_clock, "--:--"); // Chưa đồng bộ NTP: không hiển thị giờ sai.
    }

    // Icon Wi-Fi: trắng = đã kết nối, xám = đang kết nối, ẩn = không kết nối.
    const WifiStatus wifi = m_p_services->wifi().status();
    const bool show_wifi = (wifi == WifiStatus::CONNECTED) || (wifi == WifiStatus::CONNECTING);
    if (show_wifi) {
        lv_obj_set_hidden(m_p_wifi_icon, false);
        const uint32_t color = (wifi == WifiStatus::CONNECTED) ? theme::COLOR_TEXT : theme::COLOR_TEXT_MUTED;
        lv_obj_set_style_text_color(m_p_wifi_icon, lv_color_hex(color), 0);
    } else {
        lv_obj_set_hidden(m_p_wifi_icon, true);
    }
}

} // namespace fw
