/**
 * @file    launcher_app.hpp
 * @brief   [Private] Màn hình chính dạng lưới icon (home).
 */
#ifndef APPS_LAUNCHER_APP_HPP
#define APPS_LAUNCHER_APP_HPP

#include "framework/app.hpp"

namespace apps {

/** @brief Home screen: hiển thị mọi app có show_in_launcher = true. */
class LauncherApp final : public fw::App {
public:
    LauncherApp();
    void on_create(lv_obj_t* p_root) override;

private:
    void create_tile(lv_obj_t* p_parent, fw::App& target);
    static void on_tile_clicked(lv_event_t* p_event);
};

} // namespace apps

#endif /* APPS_LAUNCHER_APP_HPP */
