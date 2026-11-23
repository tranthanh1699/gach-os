/**
 * @file    settings_app.hpp
 * @brief   [Private] Cài đặt kiểu iOS: Wi-Fi, màn hình & độ sáng, ngày giờ, giới thiệu (thông tin máy), khởi động lại.
 *
 * Màn lớn: chia đôi kiểu iPad (sidebar danh mục | chi tiết). Màn nhỏ: danh sách -> trang chi tiết, Back quay lại.
 */
#ifndef APPS_SETTINGS_APP_HPP
#define APPS_SETTINGS_APP_HPP

#include <cstddef>
#include <cstdint>

#include "framework/app.hpp"

namespace apps {

/** @brief App cài đặt. Minh họa on_back() tự xử lý (hộp thoại, trang chi tiết). */
class SettingsApp final : public fw::App {
public:
    SettingsApp();
    void on_create(lv_obj_t* p_root) override;
    void on_resume() override;
    void on_pause() override;
    void on_destroy() override;
    bool on_back() override;

private:
    /** @brief Trang chi tiết (Wi-Fi mở app riêng nên không có ở đây). */
    enum class Page : uint8_t {
        NONE,
        DISPLAY,
        DATE_TIME,
        ABOUT
    };

    /** @brief Các dòng của trang Giới thiệu (cập nhật định kỳ). */
    enum AboutRow : uint8_t {
        ABOUT_NAME,
        ABOUT_CHIP,
        ABOUT_FLASH,
        ABOUT_MEMORY,
        ABOUT_MEMORY_MIN,
        ABOUT_DISPLAY,
        ABOUT_NETWORK,
        ABOUT_IP,
        ABOUT_SOFTWARE,
        ABOUT_SDK,
        ABOUT_GRAPHICS,
        ABOUT_UPTIME,
        ABOUT_ROW_COUNT
    };

    void build_menu(lv_obj_t* p_parent);
    void show_menu();
    void show_page(Page page);
    void request_page(Page page);
    lv_obj_t* begin_detail(const char* p_title);
    void build_display(lv_obj_t* p_page);
    void build_date_time(lv_obj_t* p_page);
    void build_about(lv_obj_t* p_page);
    void forget_page_widgets();
    void highlight_menu();
    void refresh();
    void refresh_wifi();
    void refresh_date_time();
    void refresh_about();
    void update_brightness_value(uint8_t percent);
    void close_popups();

    static void on_wifi_clicked(lv_event_t* p_event);
    static void on_display_clicked(lv_event_t* p_event);
    static void on_date_time_clicked(lv_event_t* p_event);
    static void on_about_clicked(lv_event_t* p_event);
    static void on_restart_clicked(lv_event_t* p_event);
    static void on_brightness_changed(lv_event_t* p_event);
    static void on_perf_overlay_changed(lv_event_t* p_event);
    static void on_timezone_changed(lv_event_t* p_event);
    static void on_dialog_confirm(lv_event_t* p_event);
    static void on_dialog_cancel(lv_event_t* p_event);
    static void on_timer(lv_timer_t* p_timer);
    static void apply_pending_page(void* p_arg);

    static constexpr size_t MENU_PAGE_COUNT = 3U; /**< DISPLAY, DATE_TIME, ABOUT. */

    lv_obj_t* m_p_root;
    lv_obj_t* m_p_detail;            /**< Khung chi tiết (màn lớn) – nội dung thay khi chọn mục. */
    lv_obj_t* m_p_menu_rows[MENU_PAGE_COUNT];
    lv_obj_t* m_p_wifi_value;
    lv_obj_t* m_p_brightness_value;
    lv_obj_t* m_p_time_value;
    lv_obj_t* m_p_sync_value;
    lv_obj_t* m_p_timezone;
    lv_obj_t* m_p_about[ABOUT_ROW_COUNT];
    lv_obj_t* m_p_dialog;
    lv_timer_t* m_p_timer;
    Page m_page;
    Page m_pending_page; /**< Trang chờ dựng (hoãn ra ngoài callback chạm: dòng được chạm có thể bị xóa). */
    bool m_is_split;
};

} // namespace apps

#endif /* APPS_SETTINGS_APP_HPP */
