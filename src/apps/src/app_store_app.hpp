/**
 * @file    app_store_app.hpp
 * @brief   [Private] Quản lý ứng dụng cài thêm: cài gói từ thẻ SD, xem danh sách, gỡ cài đặt.
 *
 * Nền móng cho kho ứng dụng: bước sau chỉ cần thêm nguồn tải (Wi-Fi) ghi gói vào install/.
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

    static void on_install_clicked(lv_event_t* p_event);
    static void on_package_clicked(lv_event_t* p_event);
    static void on_uninstall_confirmed(lv_event_t* p_event);
    static void on_dialog_cancel(lv_event_t* p_event);

    lv_obj_t* m_p_status;
    lv_obj_t* m_p_install_btn;
    lv_obj_t* m_p_list;
    lv_obj_t* m_p_dialog;
    char m_selected_id[runtime::PACKAGE_ID_BUF_LEN];
};

} // namespace apps

#endif /* APPS_APP_STORE_APP_HPP */
