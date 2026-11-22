/**
 * @file    sysinfo_app.hpp
 * @brief   [Private] Thông tin hệ thống (board, chip, heap, uptime) cập nhật mỗi giây.
 */
#ifndef APPS_SYSINFO_APP_HPP
#define APPS_SYSINFO_APP_HPP

#include "framework/app.hpp"

namespace apps {

/** @brief App thông tin hệ thống. Minh họa quản lý lv_timer theo on_resume/on_pause. */
class SysInfoApp final : public fw::App {
public:
    SysInfoApp();
    void on_create(lv_obj_t* p_root) override;
    void on_resume() override;
    void on_pause() override;

private:
    void refresh();
    static void on_timer(lv_timer_t* p_timer);

    lv_obj_t* m_p_text;
    lv_timer_t* m_p_timer;
};

} // namespace apps

#endif /* APPS_SYSINFO_APP_HPP */
