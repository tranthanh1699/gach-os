/**
 * @file    settings_app.hpp
 * @brief   [Private] Cài đặt: độ sáng màn hình, múi giờ, khởi động lại.
 */
#ifndef APPS_SETTINGS_APP_HPP
#define APPS_SETTINGS_APP_HPP

#include "framework/app.hpp"

namespace apps {

/** @brief App cài đặt. Minh họa on_back() tự xử lý (đóng hộp thoại xác nhận). */
class SettingsApp final : public fw::App {
public:
    SettingsApp();
    void on_create(lv_obj_t* p_root) override;
    void on_pause() override;
    void on_destroy() override;
    bool on_back() override;

private:
    void create_timezone_row(lv_obj_t* p_root);
    void update_brightness_label(uint8_t percent);
    void close_dialog();
    static void on_brightness_changed(lv_event_t* p_event);
    static void on_timezone_changed(lv_event_t* p_event);
    static void on_restart_clicked(lv_event_t* p_event);
    static void on_dialog_confirm(lv_event_t* p_event);
    static void on_dialog_cancel(lv_event_t* p_event);

    lv_obj_t* m_p_brightness_label;
    lv_obj_t* m_p_timezone;
    lv_obj_t* m_p_dialog;
};

} // namespace apps

#endif /* APPS_SETTINGS_APP_HPP */
