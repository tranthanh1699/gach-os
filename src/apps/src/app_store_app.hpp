/**
 * @file    app_store_app.hpp
 * @brief   [Private] Quản lý ứng dụng cài thêm: kho trực tuyến (tải + cài qua Wi-Fi), cài gói từ thẻ SD, xem
 *          danh sách, gỡ cài đặt.
 */
#ifndef APPS_APP_STORE_APP_HPP
#define APPS_APP_STORE_APP_HPP

#include <cstddef>

#include "framework/app.hpp"
#include "runtime/runtime.hpp"

namespace apps {

/** @brief App "Apps". */
class AppStoreApp final : public fw::App {
public:
    AppStoreApp();
    void on_create(lv_obj_t* p_root) override;
    void on_resume() override;
    void on_pause() override;
    bool on_back() override;

private:
    void rebuild();
    void set_status(const char* p_text, uint32_t color_hex);
    void confirm_uninstall(size_t index);
    void close_dialog();

    void change_page(int32_t delta);
    void rebuild_store();
    void store_item_clicked(size_t index);
    void show_store_message();

    static void on_mode_clicked(lv_event_t* p_event);
    static void on_refresh_clicked(lv_event_t* p_event);
    static void on_store_item_clicked(lv_event_t* p_event);
    static void on_timer(lv_timer_t* p_timer);

    static void on_install_clicked(lv_event_t* p_event);
    static void on_prev_clicked(lv_event_t* p_event);
    static void on_next_clicked(lv_event_t* p_event);
    static void on_package_clicked(lv_event_t* p_event);
    static void on_uninstall_confirmed(lv_event_t* p_event);
    static void on_dialog_cancel(lv_event_t* p_event);

    lv_obj_t* m_p_status;
    lv_obj_t* m_p_install_btn;
    lv_obj_t* m_p_mode_btn;    /**< Chuyển Store <-> Installed. */
    lv_obj_t* m_p_refresh_btn; /**< Tải lại danh mục kho. */
    lv_timer_t* m_p_timer;     /**< Theo dõi việc tải của kho (chỉ khi app đang mở). */
    bool m_store_mode;
    bool m_store_loaded;
    runtime::StoreState m_last_state;
    lv_obj_t* m_p_prev_btn;
    lv_obj_t* m_p_next_btn;
    size_t m_page; /**< Trang danh sách đang xem: chỉ dựng widget của một trang (giữ heap LVGL nhỏ). */
    lv_obj_t* m_p_list;
    lv_obj_t* m_p_dialog;
    char m_selected_id[runtime::PACKAGE_ID_BUF_LEN];
};

} // namespace apps

#endif /* APPS_APP_STORE_APP_HPP */
