/**
 * @file    script_host.hpp
 * @brief   [Private] Máy ảo Lua cho app cài thêm + bindings gach SDK v1.
 *
 * Một instance duy nhất (một vùng arena) -> tại một thời điểm chỉ một app script tồn tại
 * (AppInfo::exclusive đảm bảo điều này trong back-stack).
 *
 * An toàn:
 *  - Script chỉ thấy handle số nguyên, không bao giờ thấy con trỏ LVGL (không use-after-free).
 *  - Mỗi lần gọi vào script có ngân sách thời gian (CALL_BUDGET_MS); vượt -> lỗi "script timeout".
 *  - Lỗi script -> app hiển thị thông báo lỗi, máy ảo bị đóng; OS không bị ảnh hưởng.
 *  - Không có io/os/package/debug; cấm nạp bytecode.
 */
#ifndef RUNTIME_SCRIPT_HOST_HPP
#define RUNTIME_SCRIPT_HOST_HPP

#include <cstddef>
#include <cstdint>

#include "arena_allocator.hpp"
#include "common/non_copyable.hpp"
#include "common/status.hpp"
#include "framework/app_manager.hpp"
#include "framework/services.hpp"
#include "hal_api/board.hpp"
#include "lua.hpp"
#include "lvgl.h"
#include "manifest.hpp"
#include "runtime/runtime.hpp"

namespace runtime {

/** @brief Máy ảo Lua dùng chung cho mọi app script. */
class ScriptHost : private common::NonCopyable {
public:
    static constexpr size_t MAX_OBJECTS = 256U;   /**< Số widget tối đa một app tạo ra (bảng nằm trong PSRAM). */
    static constexpr size_t MAX_BINDINGS = 256U;  /**< Số cặp (widget, sự kiện) gắn bằng gach.ui.on (SDK v6). */
    static constexpr size_t MAX_TIMERS = 8U;      /**< Số timer đồng thời tối đa. */
    static constexpr uint32_t CALL_BUDGET_MS = 500U;
    static constexpr size_t ERROR_BUF_LEN = 160U;

    ScriptHost();

    /** @brief Gắn filesystem, vùng nhớ và HTTP client (từ hal::Devices; p_http có thể NULL). */
    void bind(hal::IFileSystem* p_fs, const hal::MemoryRegion& arena, hal::IHttpClient* p_http);

    /** @return true nếu máy ảo đang chạy một app. */
    bool is_busy() const { return m_p_lua != nullptr; }

    /**
     * @brief Khởi động app: tạo máy ảo, nạp entry, gọi app.on_create(root).
     * @note  Lỗi trong script KHÔNG làm hàm thất bại – app hiển thị màn hình lỗi.
     * @return ERR_STATE nếu đang bận, ERR_NOT_SUPPORTED nếu board không cấp arena/filesystem.
     */
    common::Status start(const Manifest& manifest, lv_obj_t* p_root, fw::AppManager& apps, fw::Services& services);

    void resume();
    void pause();
    /** @return true nếu script tự xử lý nút Back. */
    bool back();
    /** @brief Gọi app.on_destroy rồi đóng máy ảo, thu hồi toàn bộ arena. */
    void stop();

    /** @brief Trạng thái máy ảo (gach app status / inspect). */
    void stats(ScriptStats& out) const;

    /** @brief Buộc dừng: các callback vòng đời của app không được gọi nữa (stop() vẫn dọn máy ảo). */
    void kill();

    /** @return Id app đang chạy, "" nếu không có. */
    const char* running_id() const { return (m_p_lua != nullptr) ? m_manifest.id : ""; }

private:
    struct Sdk; // Bindings Lua (định nghĩa trong .cpp, truy cập thành viên private).
    friend struct UiV6; // Bindings SDK v6 (script_ui.cpp).

    /** @brief Một callback gach.ui.on: (widget, loại sự kiện) -> hàm Lua. handle = 0 là ô trống. */
    struct EventBinding {
        int16_t handle;
        uint8_t kind;
        int ref;
    };

    // ---- Cầu nối dùng chung giữa script_host.cpp và script_ui.cpp ----
    static ScriptHost* from_lua(lua_State* p_lua);
    static lv_obj_t* object_arg(lua_State* p_lua, int arg);
    static int push_object(lua_State* p_lua, lv_obj_t* p_obj, int callback_arg, lv_event_code_t code);
    static int32_t dp(lua_State* p_lua, lua_Integer value);
    static int32_t length_arg(lua_State* p_lua, int arg);
    static const lv_font_t* font_named(lua_State* p_lua, const char* p_size);

    // ---- SDK v6 (script_ui.cpp) ----
    static void register_ui_v6(lua_State* p_lua); /**< Thêm hàm v6 vào bảng gach.ui đang ở đỉnh stack. */
    static int ui_button_v6(lua_State* p_lua);     /**< gach.ui.button(parent, {opts}, fn). */
    static bool set_value_v6(lua_State* p_lua, lv_obj_t* p_obj, int arg); /**< set_value cho widget v6. */
    void release_object_extras(int32_t handle, lv_obj_t* p_obj);
    void close_system_ui();
    bool dismiss_system_ui(); /**< Back: đóng bàn phím / hộp thoại nếu đang mở. */
    void push_event_arg(lv_obj_t* p_obj, uint8_t kind, int& nargs);
    static void on_ui_event(lv_event_t* p_event);
    static void on_alert_button(lv_event_t* p_event);
    static void on_keyboard_event(lv_event_t* p_event);
    static void on_field_event(lv_event_t* p_event);
    static void on_toast_deleted(lv_event_t* p_event);
    static void on_anim_done(lv_anim_t* p_anim);
    void show_keyboard(lv_obj_t* p_field);
    void hide_keyboard();

    struct TimerSlot {
        lv_timer_t* p_timer;
        int ref;
    };

    int32_t register_object(lv_obj_t* p_obj);
    void attach_callback(int32_t handle, int ref, lv_event_code_t code);
    bool protected_call(int nargs, int nresults);
    bool call_app(const char* p_method, const int32_t* p_arg, bool* p_result);
    void invoke(int ref, const int32_t* p_arg);
    void invoke_text(int ref, const char* p_arg);
    void fault(const char* p_message);
    void close_vm();
    void show_error();
    void set_timers_paused(bool paused);

    static void on_lv_event(lv_event_t* p_event);
    static void on_timer(lv_timer_t* p_timer);
    static void on_net_poll(lv_timer_t* p_timer);
    static void async_fault(void* p_arg);
    static void async_exit(void* p_arg);
    static void timeout_hook(lua_State* p_lua, lua_Debug* p_debug);

    hal::IFileSystem* m_p_fs;
    hal::MemoryRegion m_arena;
    ArenaAllocator m_allocator;
    lua_State* m_p_lua;
    fw::AppManager* m_p_apps;
    fw::Services* m_p_services;
    lv_obj_t* m_p_root;
    Manifest m_manifest;
    lv_obj_t* m_objects[MAX_OBJECTS];
    int m_refs[MAX_OBJECTS];
    TimerSlot m_timers[MAX_TIMERS];
    uint8_t m_widget_kind[MAX_OBJECTS]; /**< Loại widget v6 tự dựng (segmented, textfield...) để đọc/ghi giá trị. */
    int m_anim_refs[MAX_OBJECTS];       /**< Callback done của gach.ui.animate đang chạy. */
    EventBinding m_bindings[MAX_BINDINGS];
    lv_obj_t* m_p_keyboard; /**< Bàn phím hệ thống (top layer), tạo khi textfield đầu tiên được chạm. */
    lv_obj_t* m_p_alert;
    int m_alert_ref;
    lv_obj_t* m_p_toast;
    hal::IHttpClient* m_p_http;
    lv_timer_t* m_p_net_timer; /**< Hỏi trạng thái yêu cầu gach.net.get đang chạy. */
    int m_net_ref;             /**< Callback Lua của yêu cầu đang chạy. */
    int m_app_ref;
    hal::FileHandle m_load_handle;
    uint32_t m_deadline;
    bool m_in_call;
    bool m_faulted;
    bool m_exit_pending;
    bool m_exiting;
    char m_error[ERROR_BUF_LEN];
};

} // namespace runtime

#endif /* RUNTIME_SCRIPT_HOST_HPP */
