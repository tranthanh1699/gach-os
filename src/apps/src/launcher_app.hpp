/**
 * @file    launcher_app.hpp
 * @brief   [Private] Màn hình chính dạng lưới icon (home).
 */
#ifndef APPS_LAUNCHER_APP_HPP
#define APPS_LAUNCHER_APP_HPP

#include <cstdint>

#include "framework/app.hpp"

namespace apps {

/** @brief Home screen: hiển thị mọi app có show_in_launcher = true. */
class LauncherApp final : public fw::App {
public:
    LauncherApp();
    void on_create(lv_obj_t* p_root) override;
    void on_resume() override;
    void on_pause() override;

private:
    void build_grid();
    void refresh_if_changed();
    static void on_timer(lv_timer_t* p_timer);
    void create_tile(lv_obj_t* p_parent, fw::App& target);
    static void on_tile_clicked(lv_event_t* p_event);

    lv_obj_t* m_p_root;
    lv_timer_t* m_p_timer;
    uint32_t m_built_generation;
};

} // namespace apps

#endif /* APPS_LAUNCHER_APP_HPP */
