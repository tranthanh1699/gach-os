/**
 * @file    wifi_app.hpp
 * @brief   [Private] Cài đặt Wi-Fi: quét mạng, nhập mật khẩu (bàn phím ảo), kết nối/ngắt.
 */
#ifndef APPS_WIFI_APP_HPP
#define APPS_WIFI_APP_HPP

#include <cstddef>
#include <cstdint>

#include "framework/app.hpp"
#include "framework/wifi_service.hpp"

namespace apps {

/** @brief App Wi-Fi. Trạng thái đọc định kỳ từ fw::WifiService (không chặn UI). */
class WifiApp final : public fw::App {
public:
    WifiApp();
    void on_create(lv_obj_t* p_root) override;
    void on_resume() override;
    void on_pause() override;
    bool on_back() override;

private:
    void create_header(lv_obj_t* p_root);
    void refresh();
    void refresh_status_label();
    void rebuild_list();
    void select_network(size_t index);
    void open_password_dialog();
    void close_password_dialog();
    void connect_selected(const char* p_password);

    static void on_timer(lv_timer_t* p_timer);
    static void on_scan_clicked(lv_event_t* p_event);
    static void on_disconnect_clicked(lv_event_t* p_event);
    static void on_network_clicked(lv_event_t* p_event);
    static void on_keyboard_event(lv_event_t* p_event);

    lv_obj_t* m_p_root;
    lv_obj_t* m_p_status;
    lv_obj_t* m_p_scan_btn;
    lv_obj_t* m_p_disconnect_btn;
    lv_obj_t* m_p_list;
    lv_obj_t* m_p_dialog;
    lv_obj_t* m_p_password;
    lv_timer_t* m_p_timer;
    uint32_t m_shown_generation;
    char m_selected_ssid[fw::WIFI_SSID_BUF_LEN];
};

} // namespace apps

#endif /* APPS_WIFI_APP_HPP */
