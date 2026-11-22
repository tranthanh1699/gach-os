/**
 * @file    builtin_apps.cpp
 * @brief   Cấp phát tĩnh và đăng ký bộ app tích hợp.
 *
 * Thêm app mới: tạo <name>_app.hpp/.cpp trong src/apps/src, khai báo instance tĩnh
 * ở đây và thêm vào mảng s_apps. Không cần sửa framework hay platform.
 */
#include "apps/builtin_apps.hpp"

#include "launcher_app.hpp"
#include "settings_app.hpp"
#include "stopwatch_app.hpp"
#include "sysinfo_app.hpp"
#include "touch_test_app.hpp"

namespace apps {

namespace {

LauncherApp s_launcher;
StopwatchApp s_stopwatch;
TouchTestApp s_touch_test;
SysInfoApp s_sysinfo;
SettingsApp s_settings;

// Thứ tự mảng = thứ tự icon trên launcher.
fw::App* const s_apps[] = {&s_launcher, &s_stopwatch, &s_touch_test, &s_sysinfo, &s_settings};

} // namespace

common::Status register_builtin_apps(fw::AppManager& manager)
{
    for (fw::App* const p_app : s_apps) {
        const common::Status status = manager.register_app(*p_app);
        if (!common::is_ok(status)) {
            return status;
        }
    }
    return manager.set_home(s_launcher.info().p_id);
}

} // namespace apps
