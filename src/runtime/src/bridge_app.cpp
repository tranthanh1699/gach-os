/**
 * @file    bridge_app.cpp
 * @brief   Lệnh `app ...` của debug bridge: liệt kê / xem / mở / đóng / buộc dừng / bật-tắt / cài / gỡ app,
 *          quyền, log riêng và tài nguyên đang dùng.
 *
 * Thao tác đổi trạng thái app (mở, đóng, bật / tắt) chạy trên task UI qua bridge_run_on_ui(); đọc danh sách giữ
 * fw::UiLock cho nhất quán với UI.
 */
#include <cstdarg>
#include <cstdio>
#include <cstdlib>
#include <cstring>

#include "bridge_sys.hpp"
#include "framework/app.hpp"
#include "framework/app_manager.hpp"
#include "framework/system.hpp"
#include "framework/ui_lock.hpp"
#include "runtime/runtime.hpp"

namespace runtime {

namespace {

constexpr size_t LINE_LEN = 200U;
constexpr size_t PATH_LEN = 128U;
constexpr size_t LOG_LINES_DEFAULT = 40U;
constexpr size_t LOG_LINES_MAX = 400U;
constexpr const char* APPS_DIR = "apps";
constexpr const char* INSTALL_DIR = "install";
constexpr uint32_t KIB = 1024U;

void line(uint8_t id, const char* p_fmt, ...) __attribute__((format(printf, 2, 3)));
void line(uint8_t id, const char* p_fmt, ...)
{
    char text[LINE_LEN];
    va_list args;
    va_start(args, p_fmt);
    (void)std::vsnprintf(text, sizeof(text), p_fmt, args);
    va_end(args);
    bridge_text(id, text);
}

enum class AppKind : uint8_t { SCRIPT, BUILTIN, SYSTEM };

AppKind kind_of(const fw::App& app)
{
    PackageDetails details{};
    if (package_details(app.info().p_id, details)) {
        return AppKind::SCRIPT;
    }
    return app.info().show_in_launcher ? AppKind::BUILTIN : AppKind::SYSTEM;
}

const char* kind_name(AppKind kind)
{
    return (kind == AppKind::SCRIPT) ? "script" : ((kind == AppKind::BUILTIN) ? "builtin" : "system");
}

/** @return "foreground", "background" (trong back-stack) hoặc "stopped". Gọi khi giữ UiLock. */
const char* state_of(const fw::App& app)
{
    fw::AppManager& apps = fw::app_manager();
    if (apps.foreground() == &app) {
        return "foreground";
    }
    for (size_t index = 0U; index < apps.stack_depth(); ++index) {
        if (apps.stack_at(index) == &app) {
            return "background";
        }
    }
    return "stopped";
}

/** @return App theo id (gọi khi giữ UiLock), NULL + đã gửi lỗi nếu không có. */
const fw::App* require_app(uint8_t id, const char* p_app)
{
    if (p_app == nullptr) {
        bridge_resp(id, false, "missing <id> (see app list)");
        return nullptr;
    }
    const fw::App* const p_found = fw::app_manager().find(p_app);
    if (p_found == nullptr) {
        bridge_resp(id, false, "no such app (see app list)");
    }
    return p_found;
}

// ============================================================================
// Đọc
// ============================================================================
void cmd_list(uint8_t id)
{
    fw::UiLock lock;
    fw::AppManager& apps = fw::app_manager();
    line(id, "%-22s %-18s %-8s %-9s %-11s %s", "ID", "NAME", "TYPE", "VERSION", "STATE", "ENABLED");
    size_t scripts = 0U;
    for (size_t index = 0U; index < apps.count(); ++index) {
        const fw::App* const p_app = apps.at(index);
        if (p_app == nullptr) {
            continue;
        }
        const AppKind kind = kind_of(*p_app);
        PackageDetails details{};
        const bool is_script = package_details(p_app->info().p_id, details);
        scripts += is_script ? 1U : 0U;
        line(id, "%-22s %-18s %-8s %-9s %-11s %s", p_app->info().p_id, p_app->info().p_name, kind_name(kind),
             is_script ? details.p_version : "-", state_of(*p_app), apps.is_enabled(p_app->info().p_id) ? "yes" : "no");
    }
    char summary[LINE_LEN];
    (void)std::snprintf(summary, sizeof(summary), "%u app(s): %u built in, %u installed", static_cast<unsigned>(apps.count()),
                        static_cast<unsigned>(apps.count() - scripts), static_cast<unsigned>(scripts));
    bridge_resp(id, true, summary);
}

void print_info(uint8_t id, const fw::App& app)
{
    fw::AppManager& apps = fw::app_manager();
    const fw::AppInfo& info = app.info();
    PackageDetails details{};
    const bool is_script = package_details(info.p_id, details);
    line(id, "id          %s", info.p_id);
    line(id, "name        %s", info.p_name);
    line(id, "type        %s%s", kind_name(kind_of(app)), apps.is_essential(info.p_id) ? " (essential)" : "");
    line(id, "state       %s", state_of(app));
    line(id, "enabled     %s", apps.is_enabled(info.p_id) ? "yes" : "no");
    line(id, "launcher    %s", info.show_in_launcher ? "visible" : "hidden (opened from another app)");
    line(id, "exclusive   %s", info.exclusive ? "yes (closes other exclusive apps)" : "no");
    if (is_script) {
        line(id, "version     %s", details.p_version);
        line(id, "sdk         v%u", static_cast<unsigned>(details.sdk));
        line(id, "entry       %s/%s/%s", APPS_DIR, details.p_id, details.p_entry);
        line(id, "icon        %s", (details.p_icon[0] != '\0') ? details.p_icon : "(default)");
    } else {
        line(id, "version     built into the firmware");
    }
}

void cmd_info(uint8_t id, const char* p_app)
{
    fw::UiLock lock;
    const fw::App* const p_found = require_app(id, p_app);
    if (p_found != nullptr) {
        print_info(id, *p_found);
        bridge_resp(id, true, nullptr);
    }
}

void cmd_permissions(uint8_t id, const char* p_app)
{
    fw::UiLock lock;
    const fw::App* const p_found = require_app(id, p_app);
    if (p_found == nullptr) {
        return;
    }
    PackageDetails details{};
    if (!package_details(p_found->info().p_id, details)) {
        line(id, "system / built-in app: full OS access (compiled into the firmware)");
        bridge_resp(id, true, nullptr);
        return;
    }
    line(id, "always      ui, timers, sys (time, stats), log, json   (sandboxed Lua VM)");
    line(id, "storage     %s  gach.storage: own key-value file apps/%s/data.ini", details.can_storage ? "granted" : "-      ",
         details.p_id);
    line(id, "network     %s  gach.net: HTTP/HTTPS requests over Wi-Fi", details.can_network ? "granted" : "-      ");
    line(id, "never       files outside its folder, other apps' data, raw hardware");
    bridge_resp(id, true, nullptr);
}

struct DirSum {
    uint8_t id;
    uint32_t files;
    uint32_t bytes;
    bool print;
};

bool sum_entry(const hal::DirEntry& entry, void* p_ctx)
{
    auto* const p_sum = static_cast<DirSum*>(p_ctx);
    ++p_sum->files;
    p_sum->bytes += entry.size;
    if (p_sum->print) {
        line(p_sum->id, "  %-28s %8u B%s", entry.name, static_cast<unsigned>(entry.size), entry.is_dir ? "  (dir)" : "");
    }
    return true;
}

void print_script_stats(uint8_t id, const char* p_app)
{
    ScriptStats stats{};
    script_stats(stats);
    if (!stats.is_running || (std::strcmp(stats.p_app_id, p_app) != 0)) {
        line(id, "script vm   not running this app");
        return;
    }
    line(id, "script vm   running%s", stats.is_faulted ? " - FAULTED (error screen)" : "");
    line(id, "lua memory  %u KB used, peak %u KB, of %u KB", static_cast<unsigned>(stats.mem_used / KIB),
         static_cast<unsigned>(stats.mem_peak / KIB), static_cast<unsigned>(stats.mem_capacity / KIB));
    line(id, "ui          %u widget(s), %u timer(s), %u event handler(s)", static_cast<unsigned>(stats.widgets),
         static_cast<unsigned>(stats.timers), static_cast<unsigned>(stats.handlers));
    if (stats.p_error[0] != '\0') {
        line(id, "last error  %s", stats.p_error);
    }
}

void cmd_inspect(uint8_t id, const char* p_app, const hal::Devices& dev)
{
    fw::UiLock lock;
    const fw::App* const p_found = require_app(id, p_app);
    if (p_found == nullptr) {
        return;
    }
    print_info(id, *p_found);
    PackageDetails details{};
    if (package_details(p_found->info().p_id, details)) {
        print_script_stats(id, details.p_id);
        char dir[PATH_LEN];
        (void)std::snprintf(dir, sizeof(dir), "%s/%s", APPS_DIR, details.p_id);
        if ((dev.p_fs != nullptr) && dev.p_fs->is_mounted()) {
            line(id, "files       %s/", dir);
            DirSum sum{id, 0U, 0U, true};
            (void)dev.p_fs->list_dir_detailed(dir, &sum_entry, &sum);
            line(id, "disk        %u file(s), %u B", static_cast<unsigned>(sum.files), static_cast<unsigned>(sum.bytes));
        }
    } else {
        fw::AppManager& apps = fw::app_manager();
        for (size_t index = 0U; index < apps.stack_depth(); ++index) {
            if (apps.stack_at(index) == p_found) {
                line(id, "back stack  position %u of %u (0 = home)", static_cast<unsigned>(index),
                     static_cast<unsigned>(apps.stack_depth()));
            }
        }
    }
    bridge_resp(id, true, nullptr);
}

void cmd_status(uint8_t id)
{
    fw::UiLock lock;
    fw::AppManager& apps = fw::app_manager();
    const fw::App* const p_fg = apps.foreground();
    line(id, "foreground  %s", (p_fg != nullptr) ? p_fg->info().p_id : "-");
    char stack[LINE_LEN] = {};
    size_t used = 0U;
    for (size_t index = 0U; (index < apps.stack_depth()) && (used < sizeof(stack)); ++index) {
        used += static_cast<size_t>(std::snprintf(&stack[used], sizeof(stack) - used, "%s%s", (index > 0U) ? " > " : "",
                                                  apps.stack_at(index)->info().p_id));
    }
    line(id, "back stack  %s  (%u open)", stack, static_cast<unsigned>(apps.stack_depth()));
    size_t disabled = 0U;
    for (size_t index = 0U; index < apps.count(); ++index) {
        disabled += apps.is_enabled(apps.at(index)->info().p_id) ? 0U : 1U;
    }
    line(id, "registered  %u app(s), %u installed package(s), %u disabled", static_cast<unsigned>(apps.count()),
         static_cast<unsigned>(package_count()), static_cast<unsigned>(disabled));
    ScriptStats stats{};
    script_stats(stats);
    if (stats.is_running) {
        line(id, "script vm   %s%s, lua %u/%u KB, %u widget(s), %u timer(s)", stats.p_app_id,
             stats.is_faulted ? " (FAULTED)" : "", static_cast<unsigned>(stats.mem_used / KIB),
             static_cast<unsigned>(stats.mem_capacity / KIB), static_cast<unsigned>(stats.widgets),
             static_cast<unsigned>(stats.timers));
    } else {
        line(id, "script vm   idle");
    }
    line(id, "storage     %s", is_available() ? "SD card mounted" : "no SD card (installed apps unavailable)");
    if (last_error()[0] != '\0') {
        line(id, "last error  %s", last_error());
    }
    bridge_resp(id, true, nullptr);
}

struct LogFilter {
    uint8_t id;
    const char* p_app;
    size_t skip;
    size_t matched;
};

bool log_matches(const char* p_line, const char* p_app)
{
    return std::strstr(p_line, p_app) != nullptr; // Tag của app = id; log của OS về app cũng nhắc id.
}

void cmd_logs(uint8_t id, const char* p_app, const char* p_count)
{
    {
        fw::UiLock lock;
        if (require_app(id, p_app) == nullptr) {
            return;
        }
    }
    size_t wanted = (p_count != nullptr) ? static_cast<size_t>(std::strtoul(p_count, nullptr, 10)) : LOG_LINES_DEFAULT;
    wanted = ((wanted == 0U) || (wanted > LOG_LINES_MAX)) ? LOG_LINES_DEFAULT : wanted;
    LogFilter filter{id, p_app, 0U, 0U};
    bridge_for_each_log_line([](const char* p_line, void* p_ctx) {
        auto* const p_filter = static_cast<LogFilter*>(p_ctx);
        p_filter->matched += log_matches(p_line, p_filter->p_app) ? 1U : 0U;
    }, &filter);
    const size_t total = filter.matched;
    filter.skip = (total > wanted) ? (total - wanted) : 0U; // Chỉ in n dòng cuối.
    filter.matched = 0U;
    bridge_for_each_log_line([](const char* p_line, void* p_ctx) {
        auto* const p_filter = static_cast<LogFilter*>(p_ctx);
        if (log_matches(p_line, p_filter->p_app) && (p_filter->matched++ >= p_filter->skip)) {
            bridge_text(p_filter->id, p_line);
        }
    }, &filter);
    char summary[LINE_LEN];
    (void)std::snprintf(summary, sizeof(summary), "%u of %u matching line(s) in the log history",
                        static_cast<unsigned>((total < wanted) ? total : wanted), static_cast<unsigned>(total));
    bridge_resp(id, true, summary);
}

// ============================================================================
// Đổi trạng thái (trên task UI)
// ============================================================================
enum class Op : uint8_t { LAUNCH, STOP, RESTART, KILL, ENABLE, DISABLE };

struct OpJob {
    Op op;
    const char* p_app;
    common::Status status;
    bool killed_script;
};

void run_op(void* p_ctx)
{
    auto* const p_job = static_cast<OpJob*>(p_ctx);
    fw::AppManager& apps = fw::app_manager();
    switch (p_job->op) {
    case Op::LAUNCH:
        p_job->status = apps.launch(p_job->p_app);
        break;
    case Op::STOP:
        p_job->status = apps.close(p_job->p_app);
        break;
    case Op::RESTART:
        (void)apps.close(p_job->p_app); // Chưa mở thì chỉ mở.
        p_job->status = apps.launch(p_job->p_app);
        break;
    case Op::KILL:
        p_job->killed_script = script_mark_killed(p_job->p_app);
        p_job->status = apps.close(p_job->p_app);
        break;
    case Op::ENABLE:
    case Op::DISABLE:
        p_job->status = apps.set_enabled(p_job->p_app, p_job->op == Op::ENABLE);
        break;
    default:
        p_job->status = common::Status::ERR_PARAM;
        break;
    }
}

void cmd_op(uint8_t id, Op op, const char* p_app)
{
    {
        fw::UiLock lock;
        const fw::App* const p_found = require_app(id, p_app);
        if (p_found == nullptr) {
            return;
        }
        fw::AppManager& apps = fw::app_manager();
        if (((op == Op::LAUNCH) || (op == Op::RESTART)) && !apps.is_enabled(p_app)) {
            bridge_resp(id, false, "app is disabled (app enable <id>)");
            return;
        }
        if ((op == Op::DISABLE) && apps.is_essential(p_app)) {
            bridge_resp(id, false, "essential app (home / settings / apps) cannot be disabled");
            return;
        }
    }
    OpJob job{op, p_app, common::Status::OK, false};
    if (!bridge_run_on_ui(&run_op, &job)) {
        bridge_resp(id, false, "ui busy (timeout) - try `uidiag`");
        return;
    }
    static const char* const DONE[] = {"launched", "stopped", "restarted", "killed", "enabled", "disabled"};
    if (common::is_ok(job.status)) {
        if ((op == Op::KILL) && !job.killed_script) {
            bridge_resp(id, true, "stopped (built-in app: closed normally, kill only skips script code)");
        } else {
            bridge_resp(id, true, DONE[static_cast<size_t>(op)]);
        }
        return;
    }
    const char* p_reason = "failed";
    if ((op == Op::STOP) || (op == Op::KILL)) {
        p_reason = (job.status == common::Status::ERR_STATE) ? "app is not running" : "cannot stop the home app";
    } else if ((op == Op::ENABLE) || (op == Op::DISABLE)) {
        p_reason = (job.status == common::Status::ERR_FULL) ? "too many disabled apps" : "cannot change this app";
    } else {
        p_reason = (job.status == common::Status::ERR_FULL) ? "too many apps open" : "cannot open the app";
    }
    bridge_resp(id, false, p_reason);
}

void cmd_install(uint8_t id, const char* p_path, const hal::Devices& dev)
{
    if (p_path != nullptr) {
        // Gói đã ở trên thẻ (VD downloads/x.gapp): chuyển vào install/ rồi cài.
        const size_t len = std::strlen(p_path);
        if ((len < sizeof(".gapp")) || (std::strcmp(&p_path[len - (sizeof(".gapp") - 1U)], ".gapp") != 0)) {
            bridge_resp(id, false, "expected a .gapp path on the SD card (from the PC: gach app install file.gapp)");
            return;
        }
        const char* const p_slash = std::strrchr(p_path, '/');
        char target[PATH_LEN];
        (void)std::snprintf(target, sizeof(target), "%s/%s", INSTALL_DIR, (p_slash != nullptr) ? (p_slash + 1) : p_path);
        if ((std::strcmp(target, p_path) != 0) && (dev.p_fs != nullptr)) {
            fw::UiLock lock;
            bool is_dir = false;
            if (dev.p_fs->exists(target, is_dir)) {
                (void)dev.p_fs->remove(target);
            }
            if (!common::is_ok(dev.p_fs->rename(p_path, target))) {
                bridge_resp(id, false, "cannot move the package into install/");
                return;
            }
        }
    }
    size_t installed = 0U;
    common::Status status = common::Status::OK;
    {
        fw::UiLock lock; // Đăng ký app vào AppManager cùng ngữ cảnh với UI.
        status = install_pending(installed);
    }
    char text[LINE_LEN];
    (void)std::snprintf(text, sizeof(text), "installed %u package(s)%s%s", static_cast<unsigned>(installed),
                        (last_error()[0] != '\0') ? " - " : "", last_error());
    bridge_resp(id, common::is_ok(status), text);
}

void cmd_uninstall(uint8_t id, const char* p_app)
{
    PackageDetails details{};
    {
        fw::UiLock lock;
        if (require_app(id, p_app) == nullptr) {
            return;
        }
        if (!package_details(p_app, details)) {
            bridge_resp(id, false, "built-in apps cannot be uninstalled (app disable <id> hides them)");
            return;
        }
    }
    // Đang mở thì đóng trước (trên task UI), rồi gỡ.
    OpJob job{Op::STOP, p_app, common::Status::OK, false};
    (void)bridge_run_on_ui(&run_op, &job);
    common::Status status = common::Status::OK;
    {
        fw::UiLock lock;
        status = uninstall(p_app);
    }
    bridge_resp(id, common::is_ok(status), common::is_ok(status) ? "uninstalled (app and data removed)" : last_error());
}

} // namespace

bool bridge_app_command(uint8_t id, size_t argc, char* const p_args[], const hal::Devices& devices)
{
    if ((argc == 0U) || (std::strcmp(p_args[0], "app") != 0)) {
        return false;
    }
    const char* const p_sub = (argc > 1U) ? p_args[1] : "list";
    const char* const p_a1 = (argc > 2U) ? p_args[2] : nullptr;
    const char* const p_a2 = (argc > 3U) ? p_args[3] : nullptr;
    const auto is = [p_sub](const char* p_name) { return std::strcmp(p_sub, p_name) == 0; };
    if (is("list")) {
        cmd_list(id);
    } else if (is("info")) {
        cmd_info(id, p_a1);
    } else if (is("inspect")) {
        cmd_inspect(id, p_a1, devices);
    } else if (is("status")) {
        cmd_status(id);
    } else if (is("permissions")) {
        cmd_permissions(id, p_a1);
    } else if (is("logs")) {
        cmd_logs(id, p_a1, p_a2);
    } else if (is("launch")) {
        cmd_op(id, Op::LAUNCH, p_a1);
    } else if (is("stop")) {
        cmd_op(id, Op::STOP, p_a1);
    } else if (is("restart")) {
        cmd_op(id, Op::RESTART, p_a1);
    } else if (is("kill")) {
        cmd_op(id, Op::KILL, p_a1);
    } else if (is("enable")) {
        cmd_op(id, Op::ENABLE, p_a1);
    } else if (is("disable")) {
        cmd_op(id, Op::DISABLE, p_a1);
    } else if (is("install")) {
        cmd_install(id, p_a1, devices);
    } else if (is("uninstall")) {
        cmd_uninstall(id, p_a1);
    } else {
        bridge_resp(id, false, "unknown app command (try: help apps)");
    }
    return true;
}

} // namespace runtime
