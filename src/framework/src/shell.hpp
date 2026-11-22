/**
 * @file    shell.hpp
 * @brief   [Private] System UI: status bar (tiêu đề + giờ) và nav bar (Back / Home).
 */
#ifndef FRAMEWORK_SHELL_HPP
#define FRAMEWORK_SHELL_HPP

#include "common/non_copyable.hpp"
#include "framework/app_manager.hpp"
#include "framework/services.hpp"
#include "lvgl.h"

namespace fw {

/** @brief Khung giao diện hệ thống bao quanh vùng nội dung của app. */
class Shell final : public IAppListener, private common::NonCopyable {
public:
    Shell();

    /**
     * @brief Dựng status bar, content area, nav bar trên màn hình hiện tại.
     * @param[in] apps     App manager (nhận lệnh Back/Home).
     * @param[in] services Dịch vụ hệ thống (uptime).
     */
    void create(AppManager& apps, const Services& services);

    /** @return Container vùng nội dung dành cho app. */
    lv_obj_t* content() const { return m_p_content; }

    void on_foreground_changed(const App& app) override;

private:
    static void on_back_clicked(lv_event_t* p_event);
    static void on_home_clicked(lv_event_t* p_event);
    static void on_status_timer(lv_timer_t* p_timer);
    lv_obj_t* create_nav_button(lv_obj_t* p_parent, const char* p_symbol, lv_event_cb_t cb);
    void refresh_status();

    AppManager* m_p_apps;
    const Services* m_p_services;
    lv_obj_t* m_p_title;
    lv_obj_t* m_p_clock;
    lv_obj_t* m_p_content;
};

} // namespace fw

#endif /* FRAMEWORK_SHELL_HPP */
