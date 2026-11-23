/**
 * @file    settings_app.cpp
 * @brief   Hiện thực app cài đặt kiểu iOS.
 */
#include "settings_app.hpp"

#include <cstdio>
#include <cstdlib>

#include "common/string_utils.hpp"
#include "framework/app_manager.hpp"
#include "framework/services.hpp"
#include "framework/theme.hpp"
#include "grouped_list.hpp"
#include "runtime/sdk_version.hpp"

namespace apps {

namespace {

constexpr fw::AppInfo INFO{"settings", "Settings", LV_SYMBOL_SETTINGS, 0x6E7681U, true};
constexpr int32_t BRIGHTNESS_MIN = 5; // Không cho tắt hẳn đèn nền -> tránh "màn hình chết".
constexpr int32_t BRIGHTNESS_MAX = 100;
constexpr uint32_t REFRESH_PERIOD_MS = 1000U;
constexpr uint32_t MS_PER_SECOND = 1000U;
constexpr uint32_t SECONDS_PER_MINUTE = 60U;
constexpr uint32_t MINUTES_PER_HOUR_U = 60U;
constexpr uint32_t HOURS_PER_DAY = 24U;
constexpr uint32_t BYTES_PER_KIB = 1024U;
constexpr uint32_t KIB_PER_MIB = 1024U;
constexpr size_t VALUE_TEXT_LEN = 48U;
constexpr int32_t SIDEBAR_WIDTH_PCT = 36;        /**< Sidebar chiếm ~1/3 màn lớn, như iPad. */
constexpr int32_t SPLIT_DIVIDER_WIDTH = 1;       /**< dp */
constexpr int32_t TIMEZONE_DROPDOWN_WIDTH = 130; /**< dp */
constexpr const char* SOFTWARE_NAME = "gach-os";
constexpr int32_t PERF_SWITCH_WIDTH = 40;  /**< dp */
constexpr int32_t PERF_SWITCH_HEIGHT = 22; /**< dp */
#if LV_USE_PERF_MONITOR
bool s_is_perf_overlay_visible = true; // LVGL bật sẵn lớp đo khi tạo display.
#endif

// Màu icon theo thói quen iOS.
constexpr uint32_t ICON_WIFI = 0x0A84FFU;
constexpr uint32_t ICON_DISPLAY = 0x5E5CE6U;
constexpr uint32_t ICON_DATE_TIME = 0xFF9F0AU;
constexpr uint32_t ICON_ABOUT = 0x8E8E93U;

// Các múi giờ đang dùng trên thế giới (phút so với UTC), gồm cả múi lẻ :30 / :45.
constexpr int16_t TIMEZONES_MIN[] = {
    -720, -660, -600, -570, -540, -480, -420, -360, -300, -240, -210, -180, -120, -60, 0,   60,  120, 180, 210,
    240,  270,  300,  330,  345,  360,  390,  420,  480,  525,  540,  570,  600,  630, 660, 720, 765, 780, 840};
constexpr size_t TIMEZONE_COUNT = sizeof(TIMEZONES_MIN) / sizeof(TIMEZONES_MIN[0]);
constexpr size_t TIMEZONE_LABEL_LEN = 10U; // "UTC+05:45\n"
constexpr int32_t MINUTES_PER_HOUR = 60;

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

const char* or_unknown(const char* p_text)
{
    return (p_text != nullptr) ? p_text : "-";
}

/** @brief Đặt chữ cho label nếu label còn tồn tại (trang có thể chưa dựng). */
void set_value(lv_obj_t* p_label, const char* p_text)
{
    if (p_label != nullptr) {
        lv_label_set_text(p_label, p_text);
    }
}

} // namespace

SettingsApp::SettingsApp()
    : fw::App(INFO), m_p_root(nullptr), m_p_detail(nullptr), m_p_menu_rows{}, m_p_wifi_value(nullptr),
      m_p_brightness_value(nullptr), m_p_time_value(nullptr), m_p_sync_value(nullptr), m_p_timezone(nullptr),
      m_p_about{}, m_p_dialog(nullptr), m_p_timer(nullptr), m_page(Page::NONE), m_pending_page(Page::NONE),
      m_is_split(false)
{
}

void SettingsApp::on_create(lv_obj_t* p_root)
{
    m_p_root = p_root;
    m_is_split = fw::theme::is_large_screen();
    lv_obj_set_style_pad_all(p_root, 0, 0);
    lv_obj_set_style_pad_gap(p_root, 0, 0);
    lv_obj_set_style_bg_color(p_root, lv_color_hex(glist::COLOR_PAGE_BG), 0);
    lv_obj_set_style_bg_opa(p_root, LV_OPA_COVER, 0);
    lv_obj_remove_flag(p_root, LV_OBJ_FLAG_SCROLLABLE);

    if (!m_is_split) {
        show_menu();
        return;
    }
    // iPad: sidebar | đường kẻ | chi tiết.
    lv_obj_set_flex_flow(p_root, LV_FLEX_FLOW_ROW);
    lv_obj_t* const p_sidebar = glist::page(p_root);
    lv_obj_set_size(p_sidebar, LV_PCT(SIDEBAR_WIDTH_PCT), LV_PCT(100));
    lv_obj_set_style_border_side(p_sidebar, LV_BORDER_SIDE_RIGHT, 0);
    lv_obj_set_style_border_width(p_sidebar, fw::theme::px(SPLIT_DIVIDER_WIDTH), 0);
    lv_obj_set_style_border_color(p_sidebar, lv_color_hex(glist::COLOR_SEPARATOR), 0);
    build_menu(p_sidebar);

    m_p_detail = lv_obj_create(p_root);
    lv_obj_remove_style_all(m_p_detail);
    lv_obj_set_height(m_p_detail, LV_PCT(100));
    lv_obj_set_flex_grow(m_p_detail, 1);
    lv_obj_set_flex_flow(m_p_detail, LV_FLEX_FLOW_COLUMN);
    lv_obj_remove_flag(m_p_detail, LV_OBJ_FLAG_SCROLLABLE);
    show_page(Page::DISPLAY);
}

void SettingsApp::build_menu(lv_obj_t* p_parent)
{
    (void)glist::large_title(p_parent, "Settings");

    glist::section_header(p_parent, nullptr);
    lv_obj_t* const p_network = glist::group(p_parent);
    m_p_wifi_value = glist::row(p_network, LV_SYMBOL_WIFI, ICON_WIFI, "Wi-Fi", &SettingsApp::on_wifi_clicked, this);

    glist::section_header(p_parent, nullptr);
    lv_obj_t* const p_general = glist::group(p_parent);
    m_p_menu_rows[0] = lv_obj_get_parent(glist::row(p_general, LV_SYMBOL_EYE_OPEN, ICON_DISPLAY, "Display & Brightness",
                                                    &SettingsApp::on_display_clicked, this));
    m_p_menu_rows[1] = lv_obj_get_parent(
        glist::row(p_general, LV_SYMBOL_BELL, ICON_DATE_TIME, "Date & Time", &SettingsApp::on_date_time_clicked, this));
    m_p_menu_rows[2] = lv_obj_get_parent(
        glist::row(p_general, LV_SYMBOL_LIST, ICON_ABOUT, "About", &SettingsApp::on_about_clicked, this));

    glist::section_header(p_parent, nullptr);
    lv_obj_t* const p_system = glist::group(p_parent);
    (void)glist::action_row(p_system, "Restart", glist::COLOR_DESTRUCTIVE, &SettingsApp::on_restart_clicked, this);
    refresh_wifi();
}

void SettingsApp::show_menu()
{
    // Chỉ dùng ở màn nhỏ: danh sách chiếm cả màn.
    forget_page_widgets();
    lv_obj_clean(m_p_root);
    m_p_menu_rows[0] = m_p_menu_rows[1] = m_p_menu_rows[2] = nullptr;
    m_page = Page::NONE;
    lv_obj_t* const p_page = glist::page(m_p_root);
    lv_obj_set_size(p_page, LV_PCT(100), LV_PCT(100));
    build_menu(p_page);
}

void SettingsApp::forget_page_widgets()
{
    // Widget của trang cũ sắp bị xóa: bỏ mọi con trỏ tới chúng.
    close_popups();
    m_p_brightness_value = nullptr;
    m_p_time_value = nullptr;
    m_p_sync_value = nullptr;
    m_p_timezone = nullptr;
    for (lv_obj_t*& p_label : m_p_about) {
        p_label = nullptr;
    }
}

lv_obj_t* SettingsApp::begin_detail(const char* p_title)
{
    forget_page_widgets();
    lv_obj_t* p_container = m_p_detail;
    if (!m_is_split) {
        lv_obj_clean(m_p_root); // Màn nhỏ: trang chi tiết thay danh sách.
        m_p_wifi_value = nullptr;
        m_p_menu_rows[0] = m_p_menu_rows[1] = m_p_menu_rows[2] = nullptr;
        p_container = m_p_root;
    } else {
        lv_obj_clean(m_p_detail);
    }
    lv_obj_t* const p_page = glist::page(p_container);
    lv_obj_set_size(p_page, LV_PCT(100), LV_PCT(100));
    (void)glist::large_title(p_page, p_title);
    return p_page;
}

void SettingsApp::show_page(Page page)
{
    m_page = page;
    switch (page) {
    case Page::DISPLAY:
        build_display(begin_detail("Display & Brightness"));
        break;
    case Page::DATE_TIME:
        build_date_time(begin_detail("Date & Time"));
        break;
    case Page::ABOUT:
        build_about(begin_detail("About"));
        break;
    case Page::NONE:
    default:
        break;
    }
    highlight_menu();
    refresh();
}

void SettingsApp::request_page(Page page)
{
    // Màn nhỏ: dựng trang chi tiết xóa cả danh sách, gồm dòng vừa được chạm -> không làm trong callback của nó.
    const bool is_queued = (m_pending_page != Page::NONE);
    m_pending_page = page;
    if (!is_queued) {
        (void)lv_async_call(&SettingsApp::apply_pending_page, this);
    }
}

void SettingsApp::apply_pending_page(void* p_arg)
{
    auto* const p_self = static_cast<SettingsApp*>(p_arg);
    const Page page = p_self->m_pending_page;
    p_self->m_pending_page = Page::NONE;
    if ((p_self->m_p_root != nullptr) && (page != Page::NONE)) {
        p_self->show_page(page);
    }
}

void SettingsApp::highlight_menu()
{
    // Sidebar iPad: dòng của trang đang mở tô màu nhấn.
    static constexpr Page PAGES[MENU_PAGE_COUNT] = {Page::DISPLAY, Page::DATE_TIME, Page::ABOUT};
    for (size_t index = 0U; index < MENU_PAGE_COUNT; ++index) {
        lv_obj_t* const p_row = m_p_menu_rows[index];
        if (p_row == nullptr) {
            continue;
        }
        const bool is_active = (PAGES[index] == m_page);
        lv_obj_set_style_bg_color(p_row, lv_color_hex(glist::COLOR_ACCENT), 0);
        lv_obj_set_style_bg_opa(p_row, is_active ? LV_OPA_COVER : LV_OPA_TRANSP, 0);
    }
}

void SettingsApp::build_display(lv_obj_t* p_page)
{
    fw::Services& services = context().services;
    glist::section_header(p_page, "BRIGHTNESS");
    lv_obj_t* const p_group = glist::group(p_page);
    m_p_brightness_value = glist::row(p_group, nullptr, 0U, "Brightness", nullptr, nullptr);

    // Dòng thanh trượt: dựng từ một dòng rỗng để có đường kẻ & lề giống các dòng khác.
    lv_obj_t* const p_slider_row = lv_obj_get_parent(glist::row(p_group, nullptr, 0U, "", nullptr, nullptr));
    lv_obj_clean(p_slider_row);
    lv_obj_t* const p_slider = lv_slider_create(p_slider_row);
    lv_obj_set_flex_grow(p_slider, 1);
    lv_slider_set_range(p_slider, BRIGHTNESS_MIN, BRIGHTNESS_MAX);
    lv_slider_set_value(p_slider, static_cast<int32_t>(services.brightness()), LV_ANIM_OFF);
    lv_obj_set_style_bg_color(p_slider, lv_color_hex(glist::COLOR_ACCENT), LV_PART_INDICATOR);
    lv_obj_add_event_cb(p_slider, &SettingsApp::on_brightness_changed, LV_EVENT_VALUE_CHANGED, this);
    if (!services.has_backlight()) {
        lv_obj_add_state(p_slider, LV_STATE_DISABLED);
    }
    update_brightness_value(services.brightness());

#if LV_USE_PERF_MONITOR
    // Lớp đo hiệu năng của LVGL (FPS, CPU%, thời gian vẽ) – chỉ có khi firmware bật LV_USE_PERF_MONITOR.
    glist::section_header(p_page, "DEVELOPER");
    lv_obj_t* const p_dev_group = glist::group(p_page);
    lv_obj_t* const p_perf_row =
        lv_obj_get_parent(glist::row(p_dev_group, nullptr, 0U, "Performance Overlay", nullptr, nullptr));
    lv_obj_t* const p_switch = lv_switch_create(p_perf_row);
    lv_obj_set_size(p_switch, fw::theme::px(PERF_SWITCH_WIDTH), fw::theme::px(PERF_SWITCH_HEIGHT));
    lv_obj_set_style_bg_color(p_switch, lv_color_hex(glist::COLOR_ACCENT),
                              static_cast<lv_style_selector_t>(LV_PART_INDICATOR) | LV_STATE_CHECKED);
    if (s_is_perf_overlay_visible) {
        lv_obj_add_state(p_switch, LV_STATE_CHECKED);
    }
    lv_obj_add_event_cb(p_switch, &SettingsApp::on_perf_overlay_changed, LV_EVENT_VALUE_CHANGED, this);
#endif
}

void SettingsApp::on_perf_overlay_changed(lv_event_t* p_event)
{
#if LV_USE_PERF_MONITOR
    s_is_perf_overlay_visible = lv_obj_has_state(lv_event_get_target_obj(p_event), LV_STATE_CHECKED);
    if (s_is_perf_overlay_visible) {
        lv_sysmon_show_performance(nullptr);
    } else {
        lv_sysmon_hide_performance(nullptr);
    }
#else
    (void)p_event;
#endif
}

void SettingsApp::build_date_time(lv_obj_t* p_page)
{
    glist::section_header(p_page, nullptr);
    lv_obj_t* const p_group = glist::group(p_page);
    m_p_time_value = glist::row(p_group, nullptr, 0U, "Time", nullptr, nullptr);
    m_p_sync_value = glist::row(p_group, nullptr, 0U, "Network Time", nullptr, nullptr);

    glist::section_header(p_page, "TIME ZONE");
    lv_obj_t* const p_tz_group = glist::group(p_page);
    lv_obj_t* const p_tz_row = lv_obj_get_parent(glist::row(p_tz_group, nullptr, 0U, "Time Zone", nullptr, nullptr));
    m_p_timezone = lv_dropdown_create(p_tz_row);
    lv_obj_set_width(m_p_timezone, fw::theme::px(TIMEZONE_DROPDOWN_WIDTH));
    lv_dropdown_set_options_static(m_p_timezone, timezone_options());
    lv_dropdown_set_selected(m_p_timezone, timezone_index(context().services.time().utc_offset_minutes()));
    lv_obj_add_event_cb(m_p_timezone, &SettingsApp::on_timezone_changed, LV_EVENT_VALUE_CHANGED, this);
}

void SettingsApp::build_about(lv_obj_t* p_page)
{
    static const char* const TITLES[ABOUT_ROW_COUNT] = {"Name",        "Chip",    "Flash",    "Free Memory",
                                                        "Lowest Free", "Display", "Wi-Fi",    "IP Address",
                                                        "Software",    "App SDK", "Graphics", "Uptime"};
    // Nhóm: thiết bị | phần cứng | mạng | phần mềm.
    static const AboutRow SECTION_START[] = {ABOUT_NAME, ABOUT_CHIP, ABOUT_NETWORK, ABOUT_SOFTWARE};
    static const char* const SECTION_TITLE[] = {nullptr, "HARDWARE", "NETWORK", "SOFTWARE"};
    constexpr size_t SECTION_COUNT = sizeof(SECTION_START) / sizeof(SECTION_START[0]);

    lv_obj_t* p_group = nullptr;
    size_t section = 0U;
    for (size_t index = 0U; index < ABOUT_ROW_COUNT; ++index) {
        if ((section < SECTION_COUNT) && (index == SECTION_START[section])) {
            glist::section_header(p_page, SECTION_TITLE[section]);
            p_group = glist::group(p_page);
            ++section;
        }
        m_p_about[index] = glist::row(p_group, nullptr, 0U, TITLES[index], nullptr, nullptr);
    }
}

void SettingsApp::on_resume()
{
    refresh();
    m_p_timer = lv_timer_create(&SettingsApp::on_timer, REFRESH_PERIOD_MS, this);
}

void SettingsApp::on_pause()
{
    close_popups(); // Hộp thoại / danh sách dropdown nằm ngoài cây p_root, phải đóng khi rời foreground.
    if (m_p_timer != nullptr) {
        lv_timer_delete(m_p_timer);
        m_p_timer = nullptr;
    }
}

void SettingsApp::on_destroy()
{
    forget_page_widgets();
    m_p_wifi_value = nullptr;
    m_p_detail = nullptr;
    m_p_root = nullptr;
    m_page = Page::NONE;
}

bool SettingsApp::on_back()
{
    if ((m_p_dialog != nullptr) || ((m_p_timezone != nullptr) && lv_dropdown_is_open(m_p_timezone))) {
        close_popups();
        return true;
    }
    if (!m_is_split && (m_page != Page::NONE)) {
        show_menu(); // Màn nhỏ: Back từ trang chi tiết về danh sách.
        return true;
    }
    return false;
}

void SettingsApp::close_popups()
{
    if (m_p_dialog != nullptr) {
        lv_msgbox_close_async(m_p_dialog); // Async: có thể đang ở trong callback của chính dialog.
        m_p_dialog = nullptr;
    }
    if ((m_p_timezone != nullptr) && lv_dropdown_is_open(m_p_timezone)) {
        lv_dropdown_close(m_p_timezone);
    }
}

void SettingsApp::refresh()
{
    refresh_wifi();
    refresh_date_time();
    refresh_about();
}

void SettingsApp::refresh_wifi()
{
    if (m_p_wifi_value == nullptr) {
        return;
    }
    const fw::WifiService& wifi = context().services.wifi();
    const char* p_status = "Off";
    switch (wifi.status()) {
    case fw::WifiStatus::UNAVAILABLE:
        p_status = "Not Available";
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
    lv_label_set_text(m_p_wifi_value, p_status);
}

void SettingsApp::refresh_date_time()
{
    if (m_p_time_value == nullptr) {
        return;
    }
    const fw::TimeService& clock = context().services.time();
    char text[VALUE_TEXT_LEN] = {};
    fw::DateTime now{};
    if (clock.now(now)) {
        (void)std::snprintf(text, sizeof(text), "%04u-%02u-%02u  %02u:%02u:%02u", static_cast<unsigned>(now.year),
                            static_cast<unsigned>(now.month), static_cast<unsigned>(now.day),
                            static_cast<unsigned>(now.hour), static_cast<unsigned>(now.minute),
                            static_cast<unsigned>(now.second));
    } else {
        (void)common::copy_string(text, sizeof(text), "--:--");
    }
    set_value(m_p_time_value, text);
    set_value(m_p_sync_value, clock.is_synced() ? "Synced" : "Waiting for Wi-Fi");
}

void SettingsApp::refresh_about()
{
    if (m_p_about[ABOUT_NAME] == nullptr) {
        return;
    }
    fw::Services& services = context().services;
    fw::DeviceInfo info{};
    services.device_info(info);
    char text[VALUE_TEXT_LEN] = {};

    set_value(m_p_about[ABOUT_NAME], or_unknown(info.p_board_name));
    (void)std::snprintf(text, sizeof(text), "%s, %u cores @ %u MHz", or_unknown(info.p_chip_name),
                        static_cast<unsigned>(info.cpu_cores), static_cast<unsigned>(info.cpu_freq_mhz));
    set_value(m_p_about[ABOUT_CHIP], text);
    (void)std::snprintf(text, sizeof(text), "%u MB", static_cast<unsigned>(info.flash_size_kb / KIB_PER_MIB));
    set_value(m_p_about[ABOUT_FLASH], text);
    (void)std::snprintf(text, sizeof(text), "%u KB", static_cast<unsigned>(info.heap_free_bytes / BYTES_PER_KIB));
    set_value(m_p_about[ABOUT_MEMORY], text);
    (void)std::snprintf(text, sizeof(text), "%u KB", static_cast<unsigned>(info.heap_min_bytes / BYTES_PER_KIB));
    set_value(m_p_about[ABOUT_MEMORY_MIN], text);
    (void)std::snprintf(text, sizeof(text), "%u x %u%s", static_cast<unsigned>(info.screen_width),
                        static_cast<unsigned>(info.screen_height), info.has_touch ? ", touch" : "");
    set_value(m_p_about[ABOUT_DISPLAY], text);

    const fw::WifiService& wifi = services.wifi();
    const bool is_online = (wifi.status() == fw::WifiStatus::CONNECTED);
    set_value(m_p_about[ABOUT_NETWORK], is_online ? wifi.current_ssid() : "Not Connected");
    char ip[fw::IP_ADDRESS_BUF_LEN] = {};
    set_value(m_p_about[ABOUT_IP], (is_online && wifi.ip_address(ip, sizeof(ip))) ? ip : "-");

    set_value(m_p_about[ABOUT_SOFTWARE], SOFTWARE_NAME);
    (void)std::snprintf(text, sizeof(text), "v%u", static_cast<unsigned>(runtime::SDK_VERSION));
    set_value(m_p_about[ABOUT_SDK], text);
    (void)std::snprintf(text, sizeof(text), "LVGL %d.%d.%d", LVGL_VERSION_MAJOR, LVGL_VERSION_MINOR,
                        LVGL_VERSION_PATCH);
    set_value(m_p_about[ABOUT_GRAPHICS], text);

    const uint32_t total_s = services.uptime_ms() / MS_PER_SECOND;
    const uint32_t minutes = total_s / SECONDS_PER_MINUTE;
    const uint32_t hours = minutes / MINUTES_PER_HOUR_U;
    (void)std::snprintf(text, sizeof(text), "%ud %02u:%02u:%02u", static_cast<unsigned>(hours / HOURS_PER_DAY),
                        static_cast<unsigned>(hours % HOURS_PER_DAY),
                        static_cast<unsigned>(minutes % MINUTES_PER_HOUR_U),
                        static_cast<unsigned>(total_s % SECONDS_PER_MINUTE));
    set_value(m_p_about[ABOUT_UPTIME], text);
}

void SettingsApp::update_brightness_value(uint8_t percent)
{
    if (m_p_brightness_value != nullptr) {
        lv_label_set_text_fmt(m_p_brightness_value, "%u%%", static_cast<unsigned>(percent));
    }
}

void SettingsApp::on_wifi_clicked(lv_event_t* p_event)
{
    (void)static_cast<SettingsApp*>(lv_event_get_user_data(p_event))->context().apps.launch("wifi");
}

void SettingsApp::on_display_clicked(lv_event_t* p_event)
{
    static_cast<SettingsApp*>(lv_event_get_user_data(p_event))->request_page(Page::DISPLAY);
}

void SettingsApp::on_date_time_clicked(lv_event_t* p_event)
{
    static_cast<SettingsApp*>(lv_event_get_user_data(p_event))->request_page(Page::DATE_TIME);
}

void SettingsApp::on_about_clicked(lv_event_t* p_event)
{
    static_cast<SettingsApp*>(lv_event_get_user_data(p_event))->request_page(Page::ABOUT);
}

void SettingsApp::on_brightness_changed(lv_event_t* p_event)
{
    auto* const p_self = static_cast<SettingsApp*>(lv_event_get_user_data(p_event));
    const auto percent = static_cast<uint8_t>(lv_slider_get_value(lv_event_get_target_obj(p_event)));
    if (common::is_ok(p_self->context().services.set_brightness(percent))) {
        p_self->update_brightness_value(percent);
    }
}

void SettingsApp::on_timezone_changed(lv_event_t* p_event)
{
    auto* const p_self = static_cast<SettingsApp*>(lv_event_get_user_data(p_event));
    const uint32_t index = lv_dropdown_get_selected(lv_event_get_target_obj(p_event));
    if (index < TIMEZONE_COUNT) {
        // Lưu NVS qua TimeService; status bar cập nhật ở lần làm mới kế tiếp (<= 1 s).
        (void)p_self->context().services.time().set_utc_offset_minutes(TIMEZONES_MIN[index]);
        p_self->refresh_date_time();
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
    lv_obj_set_style_bg_color(p_ok, lv_color_hex(glist::COLOR_DESTRUCTIVE), 0);
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
    static_cast<SettingsApp*>(lv_event_get_user_data(p_event))->close_popups();
}

void SettingsApp::on_timer(lv_timer_t* p_timer)
{
    static_cast<SettingsApp*>(lv_timer_get_user_data(p_timer))->refresh();
}

} // namespace apps
