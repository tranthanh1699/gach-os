/**
 * @file    script_host.cpp
 * @brief   Hiện thực máy ảo Lua và gach SDK v1.
 *
 * Lưu ý longjmp: hàm C gọi từ Lua (Sdk::*) có thể bị luaL_error nhảy ra -> KHÔNG dùng đối tượng
 * có destructor không tầm thường và KHÔNG giữ file mở qua điểm có thể lỗi.
 */
#include "script_host.hpp"

#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <initializer_list>

#include "common/string_utils.hpp"
#include "framework/theme.hpp"
#include "fs_utils.hpp"
#include "osal/osal.hpp"
#include "runtime/sdk_version.hpp"

namespace runtime {

namespace {

constexpr int HOOK_INSTRUCTION_COUNT = 10000;
constexpr size_t READ_CHUNK = 256U;
constexpr int32_t ROOT_HANDLE = 1;
constexpr int32_t CONTAINER_GAP = 6;
constexpr size_t KV_BUF_LEN = 1024U;
constexpr size_t KV_KEY_MAX = 24U;
constexpr size_t KV_VALUE_MAX = 128U;
constexpr const char* APPS_DIR = "apps";
constexpr const char* DATA_FILE = "data.ini";

// Buffer key-value dùng chung (chỉ UI task, một app tại một thời điểm).
char s_kv_in[KV_BUF_LEN];
char s_kv_out[KV_BUF_LEN];

struct LoadContext {
    hal::IFileSystem* p_fs;
    hal::FileHandle handle;
    char buf[READ_CHUNK];
};

const char* file_reader(lua_State* p_lua, void* p_ud, size_t* p_size)
{
    (void)p_lua;
    auto* const p_ctx = static_cast<LoadContext*>(p_ud);
    size_t got = 0U;
    if (!common::is_ok(p_ctx->p_fs->read(p_ctx->handle, reinterpret_cast<uint8_t*>(p_ctx->buf), READ_CHUNK, got))) {
        got = 0U;
    }
    *p_size = got;
    return (got > 0U) ? p_ctx->buf : nullptr;
}

void* arena_alloc(void* p_ud, void* p_ptr, size_t old_size, size_t new_size)
{
    (void)old_size;
    auto* const p_arena = static_cast<ArenaAllocator*>(p_ud);
    if (new_size == 0U) {
        p_arena->release(p_ptr);
        return nullptr;
    }
    return p_arena->reallocate(p_ptr, new_size);
}

constexpr uint8_t SDK_SCALED_UI = 3U; /**< Từ SDK v3: kích thước app tính theo dp, font co giãn. */

ScriptHost* host_of(lua_State* p_lua)
{
    return *static_cast<ScriptHost**>(lua_getextraspace(p_lua));
}

bool is_valid_key(const char* p_key)
{
    const size_t len = std::strlen(p_key);
    if ((len == 0U) || (len > KV_KEY_MAX)) {
        return false;
    }
    for (size_t index = 0U; index < len; ++index) {
        const char ch = p_key[index];
        if (!(((ch >= 'a') && (ch <= 'z')) || ((ch >= 'A') && (ch <= 'Z')) || ((ch >= '0') && (ch <= '9')) ||
              (ch == '_'))) {
            return false;
        }
    }
    return true;
}

/** @brief Tìm "key=value\n" trong buffer; trả con trỏ tới value và độ dài. */
const char* kv_find(const char* p_buf, size_t len, const char* p_key, size_t& value_len)
{
    const size_t key_len = std::strlen(p_key);
    size_t pos = 0U;
    while (pos < len) {
        size_t end = pos;
        while ((end < len) && (p_buf[end] != '\n')) {
            ++end;
        }
        if (((end - pos) > key_len) && (std::memcmp(&p_buf[pos], p_key, key_len) == 0) &&
            (p_buf[pos + key_len] == '=')) {
            value_len = end - (pos + key_len + 1U);
            return &p_buf[pos + key_len + 1U];
        }
        pos = end + 1U;
    }
    return nullptr;
}

} // namespace

// ============================================================================
// gach SDK v1 bindings
// ============================================================================
struct ScriptHost::Sdk {
    static lv_obj_t* object(lua_State* p_lua, int arg)
    {
        ScriptHost* const p_host = host_of(p_lua);
        const lua_Integer handle = luaL_checkinteger(p_lua, arg);
        if ((handle < 1) || (handle > static_cast<lua_Integer>(MAX_OBJECTS)) ||
            (p_host->m_objects[handle - 1] == nullptr)) {
            (void)luaL_error(p_lua, "invalid UI handle %d", static_cast<int>(handle));
        }
        return p_host->m_objects[handle - 1];
    }

    static int push_new(lua_State* p_lua, lv_obj_t* p_obj, int callback_arg, lv_event_code_t code)
    {
        ScriptHost* const p_host = host_of(p_lua);
        const int32_t handle = p_host->register_object(p_obj);
        if (handle == 0) {
            lv_obj_delete(p_obj);
            return luaL_error(p_lua, "too many UI objects (max %d)", static_cast<int>(MAX_OBJECTS));
        }
        if ((callback_arg > 0) && lua_isfunction(p_lua, callback_arg)) {
            lua_pushvalue(p_lua, callback_arg);
            p_host->attach_callback(handle, luaL_ref(p_lua, LUA_REGISTRYINDEX), code);
        }
        lua_pushinteger(p_lua, handle);
        return 1;
    }

    static lv_obj_t* container(lv_obj_t* p_parent, lv_flex_flow_t flow)
    {
        lv_obj_t* const p_box = lv_obj_create(p_parent);
        lv_obj_remove_style_all(p_box);
        lv_obj_set_size(p_box, LV_PCT(100), LV_SIZE_CONTENT);
        lv_obj_set_flex_flow(p_box, flow);
        lv_obj_set_style_pad_gap(p_box, CONTAINER_GAP, 0);
        lv_obj_set_flex_align(p_box, LV_FLEX_ALIGN_START, LV_FLEX_ALIGN_CENTER, LV_FLEX_ALIGN_CENTER);
        // Container chỉ để bố cục: không nhận chạm, để click rơi xuống nút chứa nó (VD: ký hiệu vẽ trong nút).
        lv_obj_remove_flag(p_box, LV_OBJ_FLAG_CLICKABLE);
        return p_box;
    }

    // ---- gach.ui ----
    static int label(lua_State* p_lua)
    {
        lv_obj_t* const p_parent = object(p_lua, 1);
        const char* const p_text = luaL_optstring(p_lua, 2, "");
        lv_obj_t* const p_label = lv_label_create(p_parent);
        lv_label_set_text(p_label, p_text);
        lv_obj_set_style_text_color(p_label, lv_color_hex(fw::theme::COLOR_TEXT), 0);
        return push_new(p_lua, p_label, 0, LV_EVENT_ALL);
    }

    static int button(lua_State* p_lua)
    {
        lv_obj_t* const p_parent = object(p_lua, 1);
        const char* const p_text = luaL_checkstring(p_lua, 2);
        lv_obj_t* const p_btn = lv_button_create(p_parent);
        lv_obj_t* const p_label = lv_label_create(p_btn);
        lv_label_set_text(p_label, p_text);
        lv_obj_center(p_label);
        return push_new(p_lua, p_btn, 3, LV_EVENT_CLICKED);
    }

    static int column(lua_State* p_lua)
    {
        return push_new(p_lua, container(object(p_lua, 1), LV_FLEX_FLOW_COLUMN), 0, LV_EVENT_ALL);
    }

    static int row(lua_State* p_lua)
    {
        return push_new(p_lua, container(object(p_lua, 1), LV_FLEX_FLOW_ROW_WRAP), 0, LV_EVENT_ALL);
    }

    static int list(lua_State* p_lua)
    {
        lv_obj_t* const p_list = lv_list_create(object(p_lua, 1));
        lv_obj_set_width(p_list, LV_PCT(100));
        lv_obj_set_flex_grow(p_list, 1);
        return push_new(p_lua, p_list, 0, LV_EVENT_ALL);
    }

    static int list_add(lua_State* p_lua)
    {
        lv_obj_t* const p_list = object(p_lua, 1);
        const char* const p_text = luaL_checkstring(p_lua, 2);
        return push_new(p_lua, lv_list_add_button(p_list, nullptr, p_text), 3, LV_EVENT_CLICKED);
    }

    static int slider(lua_State* p_lua)
    {
        lv_obj_t* const p_parent = object(p_lua, 1);
        const auto min = static_cast<int32_t>(luaL_checkinteger(p_lua, 2));
        const auto max = static_cast<int32_t>(luaL_checkinteger(p_lua, 3));
        const auto value = static_cast<int32_t>(luaL_optinteger(p_lua, 4, min));
        luaL_argcheck(p_lua, min < max, 3, "max must be greater than min");
        lv_obj_t* const p_slider = lv_slider_create(p_parent);
        lv_obj_set_width(p_slider, LV_PCT(90));
        lv_slider_set_range(p_slider, min, max);
        lv_slider_set_value(p_slider, value, LV_ANIM_OFF);
        return push_new(p_lua, p_slider, 5, LV_EVENT_VALUE_CHANGED);
    }

    static int set_text(lua_State* p_lua)
    {
        lv_obj_t* p_obj = object(p_lua, 1);
        const char* const p_text = luaL_checkstring(p_lua, 2);
        if (!lv_obj_check_type(p_obj, &lv_label_class)) {
            p_obj = lv_obj_get_child(p_obj, 0); // Nút/mục list: đổi chữ của label con.
        }
        if ((p_obj != nullptr) && lv_obj_check_type(p_obj, &lv_label_class)) {
            lv_label_set_text(p_obj, p_text);
        }
        return 0;
    }

    // ---- Co giãn theo màn hình (SDK v3) ----
    /** @return true nếu app khai báo sdk >= 3: kích thước là dp, font co giãn theo màn hình. */
    static bool is_scaled(lua_State* p_lua) { return host_of(p_lua)->m_manifest.sdk >= SDK_SCALED_UI; }

    /** @brief Kích thước app truyền vào -> pixel thật (app đời cũ: giữ nguyên pixel như đã công bố). */
    static int32_t to_px(lua_State* p_lua, lua_Integer value)
    {
        const auto length = static_cast<int32_t>(value);
        return is_scaled(p_lua) ? fw::theme::px(length) : length;
    }

    /** @brief Font theo tên cỡ; app đời cũ dùng đúng cỡ 14/20/28 px đã công bố. */
    static const lv_font_t* font_for(lua_State* p_lua, fw::theme::FontSize size)
    {
        return is_scaled(p_lua) ? fw::theme::font(size) : fw::theme::base_font(size);
    }

    static int set_font(lua_State* p_lua)
    {
        lv_obj_t* const p_obj = object(p_lua, 1);
        const char* const p_size = luaL_checkstring(p_lua, 2);
        fw::theme::FontSize size = fw::theme::FontSize::SMALL; // "small" hoặc giá trị lạ -> font mặc định.
        if (std::strcmp(p_size, "large") == 0) {
            size = fw::theme::FontSize::LARGE;
        } else if (std::strcmp(p_size, "medium") == 0) {
            size = fw::theme::FontSize::MEDIUM;
        }
        lv_obj_set_style_text_font(p_obj, font_for(p_lua, size), 0);
        return 0;
    }

    /**
     * @brief gach.ui.on_swipe(h, fn): gọi fn("up"|"down"|"left"|"right") khi vuốt trên widget (SDK v3).
     *
     * Widget được bật nhận chạm và tắt cuộn (cuộn sẽ "nuốt" cử chỉ vuốt). Cử chỉ bắt đầu trên widget con
     * không có callback (box, label, row, column) cũng nổi lên widget này -> thường gắn vào root.
     */
    static int on_swipe(lua_State* p_lua)
    {
        ScriptHost* const p_host = host_of(p_lua);
        lv_obj_t* const p_obj = object(p_lua, 1);
        const auto handle = static_cast<int32_t>(luaL_checkinteger(p_lua, 1));
        luaL_checktype(p_lua, 2, LUA_TFUNCTION);
        luaL_argcheck(p_lua, p_host->m_refs[handle - 1] == LUA_NOREF, 1, "widget already has a callback");
        lv_obj_add_flag(p_obj, LV_OBJ_FLAG_CLICKABLE);
        lv_obj_remove_flag(p_obj, LV_OBJ_FLAG_SCROLLABLE);
        lua_pushvalue(p_lua, 2);
        p_host->attach_callback(handle, luaL_ref(p_lua, LUA_REGISTRYINDEX), LV_EVENT_GESTURE);
        return 0;
    }

    /** @brief gach.ui.screen_size() -> w, h: vùng nội dung của app (đơn vị app dùng: dp nếu sdk>=3). */
    static int screen_size(lua_State* p_lua)
    {
        lv_obj_t* const p_root = host_of(p_lua)->m_p_root;
        lv_obj_update_layout(p_root);
        int32_t width = lv_obj_get_content_width(p_root);
        int32_t height = lv_obj_get_content_height(p_root);
        if (is_scaled(p_lua)) {
            width = (width * fw::theme::SCALE_100) / fw::theme::scale_pct();
            height = (height * fw::theme::SCALE_100) / fw::theme::scale_pct();
        }
        lua_pushinteger(p_lua, width);
        lua_pushinteger(p_lua, height);
        return 2;
    }

    static int set_color(lua_State* p_lua)
    {
        lv_obj_t* const p_obj = object(p_lua, 1);
        const auto color = static_cast<uint32_t>(luaL_checkinteger(p_lua, 2));
        lv_obj_set_style_text_color(p_obj, lv_color_hex(color), 0);
        return 0;
    }

    static int clear(lua_State* p_lua)
    {
        lv_obj_clean(object(p_lua, 1)); // Sự kiện DELETE tự thu hồi handle của các con.
        return 0;
    }

    static int destroy(lua_State* p_lua)
    {
        lv_obj_t* const p_obj = object(p_lua, 1);
        luaL_argcheck(p_lua, p_obj != host_of(p_lua)->m_p_root, 1, "cannot delete root");
        lv_obj_delete(p_obj);
        return 0;
    }

    // ---- gach.ui (SDK v2: bố cục & kiểu dáng) ----
    /** @brief Độ dài: số pixel, chuỗi "N%" (theo cha) hoặc "content" (vừa nội dung). */
    static int32_t length_arg(lua_State* p_lua, int arg)
    {
        if (lua_type(p_lua, arg) == LUA_TNUMBER) {
            return to_px(p_lua, luaL_checkinteger(p_lua, arg));
        }
        size_t len = 0U;
        const char* const p_text = luaL_checklstring(p_lua, arg, &len);
        if (std::strcmp(p_text, "content") == 0) {
            return LV_SIZE_CONTENT;
        }
        if ((len > 1U) && (p_text[len - 1U] == '%')) {
            const long percent = std::strtol(p_text, nullptr, 10);
            luaL_argcheck(p_lua, (percent >= 0) && (percent <= 100), arg, "percent must be 0..100");
            return lv_pct(static_cast<int32_t>(percent));
        }
        (void)luaL_argerror(p_lua, arg, "expected pixels, \"N%\" or \"content\"");
        return 0;
    }

    static int box(lua_State* p_lua)
    {
        lv_obj_t* const p_box = lv_obj_create(object(p_lua, 1));
        lv_obj_remove_style_all(p_box);
        lv_obj_set_size(p_box, LV_SIZE_CONTENT, LV_SIZE_CONTENT);
        lv_obj_remove_flag(p_box, LV_OBJ_FLAG_CLICKABLE); // Hình trang trí: click rơi xuống widget cha.
        lv_obj_remove_flag(p_box, LV_OBJ_FLAG_SCROLLABLE);
        return push_new(p_lua, p_box, 0, LV_EVENT_ALL);
    }

    static int set_size(lua_State* p_lua)
    {
        lv_obj_t* const p_obj = object(p_lua, 1);
        if (!lua_isnoneornil(p_lua, 2)) {
            lv_obj_set_width(p_obj, length_arg(p_lua, 2));
        }
        if (!lua_isnoneornil(p_lua, 3)) {
            lv_obj_set_height(p_obj, length_arg(p_lua, 3));
        }
        return 0;
    }

    static int set_grow(lua_State* p_lua)
    {
        lv_obj_t* const p_obj = object(p_lua, 1);
        const lua_Integer grow = luaL_checkinteger(p_lua, 2);
        luaL_argcheck(p_lua, (grow >= 0) && (grow <= UINT8_MAX), 2, "grow must be 0..255");
        lv_obj_set_flex_grow(p_obj, static_cast<uint8_t>(grow));
        return 0;
    }

    static int set_bg(lua_State* p_lua)
    {
        lv_obj_t* const p_obj = object(p_lua, 1);
        const auto color = static_cast<uint32_t>(luaL_checkinteger(p_lua, 2));
        lv_obj_set_style_bg_color(p_obj, lv_color_hex(color), 0);
        lv_obj_set_style_bg_opa(p_obj, LV_OPA_COVER, 0);
        lv_obj_set_style_shadow_width(p_obj, 0, 0); // Kiểu phẳng: màu do app quyết định.
        return 0;
    }

    static int set_radius(lua_State* p_lua)
    {
        lv_obj_t* const p_obj = object(p_lua, 1);
        int32_t radius = 0;
        if ((lua_type(p_lua, 2) == LUA_TSTRING) && (std::strcmp(lua_tostring(p_lua, 2), "round") == 0)) {
            radius = LV_RADIUS_CIRCLE;
        } else {
            radius = to_px(p_lua, luaL_checkinteger(p_lua, 2));
        }
        lv_obj_set_style_radius(p_obj, radius, 0);
        return 0;
    }

    static int set_align(lua_State* p_lua)
    {
        lv_obj_t* const p_obj = object(p_lua, 1);
        static const char* const OPTIONS[] = {"left", "center", "right", nullptr};
        static const lv_text_align_t ALIGNS[] = {LV_TEXT_ALIGN_LEFT, LV_TEXT_ALIGN_CENTER, LV_TEXT_ALIGN_RIGHT};
        lv_obj_set_style_text_align(p_obj, ALIGNS[luaL_checkoption(p_lua, 2, nullptr, OPTIONS)], 0);
        return 0;
    }

    static int center(lua_State* p_lua)
    {
        lv_obj_center(object(p_lua, 1));
        return 0;
    }

    static int set_pad(lua_State* p_lua)
    {
        lv_obj_set_style_pad_all(object(p_lua, 1), to_px(p_lua, luaL_checkinteger(p_lua, 2)), 0);
        return 0;
    }

    static int set_gap(lua_State* p_lua)
    {
        lv_obj_set_style_pad_gap(object(p_lua, 1), to_px(p_lua, luaL_checkinteger(p_lua, 2)), 0);
        return 0;
    }

    /** @brief gach.ui.SYMBOL: ký hiệu có sẵn trong font hệ thống (chữ không có trong font sẽ hiện ô trống). */
    static void push_symbols(lua_State* p_lua)
    {
        static const struct {
            const char* p_name;
            const char* p_glyph;
        } SYMBOLS[] = {
            {"PLUS", LV_SYMBOL_PLUS},     {"MINUS", LV_SYMBOL_MINUS},   {"CLOSE", LV_SYMBOL_CLOSE},
            {"OK", LV_SYMBOL_OK},         {"LEFT", LV_SYMBOL_LEFT},     {"RIGHT", LV_SYMBOL_RIGHT},
            {"UP", LV_SYMBOL_UP},         {"DOWN", LV_SYMBOL_DOWN},     {"HOME", LV_SYMBOL_HOME},
            {"SETTINGS", LV_SYMBOL_SETTINGS}, {"REFRESH", LV_SYMBOL_REFRESH}, {"PLAY", LV_SYMBOL_PLAY},
            {"PAUSE", LV_SYMBOL_PAUSE},   {"STOP", LV_SYMBOL_STOP},     {"BELL", LV_SYMBOL_BELL},
            {"WIFI", LV_SYMBOL_WIFI},     {"TRASH", LV_SYMBOL_TRASH},   {"EDIT", LV_SYMBOL_EDIT},
            {"BACKSPACE", LV_SYMBOL_BACKSPACE}, {"WARNING", LV_SYMBOL_WARNING},
        };
        lua_createtable(p_lua, 0, static_cast<int>(sizeof(SYMBOLS) / sizeof(SYMBOLS[0])));
        for (const auto& symbol : SYMBOLS) {
            lua_pushstring(p_lua, symbol.p_glyph);
            lua_setfield(p_lua, -2, symbol.p_name);
        }
    }

    // ---- gach.timer ----
    static int timer_every(lua_State* p_lua)
    {
        ScriptHost* const p_host = host_of(p_lua);
        const lua_Integer period = luaL_checkinteger(p_lua, 1);
        luaL_checktype(p_lua, 2, LUA_TFUNCTION);
        luaL_argcheck(p_lua, period >= 10, 1, "period must be >= 10 ms");
        for (size_t index = 0U; index < MAX_TIMERS; ++index) {
            TimerSlot& slot = p_host->m_timers[index];
            if (slot.p_timer == nullptr) {
                lua_pushvalue(p_lua, 2);
                slot.ref = luaL_ref(p_lua, LUA_REGISTRYINDEX);
                slot.p_timer = lv_timer_create(&ScriptHost::on_timer, static_cast<uint32_t>(period), p_host);
                lua_pushinteger(p_lua, static_cast<lua_Integer>(index + 1U));
                return 1;
            }
        }
        return luaL_error(p_lua, "too many timers (max %d)", static_cast<int>(MAX_TIMERS));
    }

    static int timer_cancel(lua_State* p_lua)
    {
        ScriptHost* const p_host = host_of(p_lua);
        const lua_Integer id = luaL_checkinteger(p_lua, 1);
        if ((id >= 1) && (id <= static_cast<lua_Integer>(MAX_TIMERS))) {
            TimerSlot& slot = p_host->m_timers[id - 1];
            if (slot.p_timer != nullptr) {
                lv_timer_delete(slot.p_timer);
                luaL_unref(p_lua, LUA_REGISTRYINDEX, slot.ref);
                slot = TimerSlot{nullptr, LUA_NOREF};
            }
        }
        return 0;
    }

    // ---- gach.sys ----
    static int uptime_ms(lua_State* p_lua)
    {
        lua_pushinteger(p_lua, static_cast<lua_Integer>(host_of(p_lua)->m_p_services->uptime_ms()));
        return 1;
    }

    static int time(lua_State* p_lua)
    {
        fw::DateTime now{};
        const fw::TimeService& clock = host_of(p_lua)->m_p_services->time();
        if (!clock.now(now)) {
            lua_pushnil(p_lua);
            return 1;
        }
        lua_createtable(p_lua, 0, 8);
        lua_pushinteger(p_lua, static_cast<lua_Integer>(clock.utc_offset_minutes()));
        lua_setfield(p_lua, -2, "utc_offset"); // Phút so với UTC (múi giờ chọn trong Settings).
        const struct {
            const char* p_key;
            lua_Integer value;
        } fields[] = {{"year", now.year},  {"month", now.month},   {"day", now.day},       {"hour", now.hour},
                      {"min", now.minute}, {"sec", now.second}, {"wday", now.weekday}};
        for (const auto& field : fields) {
            lua_pushinteger(p_lua, field.value);
            lua_setfield(p_lua, -2, field.p_key);
        }
        return 1;
    }

    // ---- gach.storage (cần quyền "storage") ----
    static void require_storage(lua_State* p_lua)
    {
        if ((host_of(p_lua)->m_manifest.permissions & PERMISSION_STORAGE) == 0U) {
            (void)luaL_error(p_lua, "permission 'storage' not declared in manifest");
        }
    }

    static bool data_path(const ScriptHost& host, char* p_out, size_t out_len)
    {
        char dir[PATH_BUF_LEN];
        return join_path(dir, sizeof(dir), APPS_DIR, host.m_manifest.id) && join_path(p_out, out_len, dir, DATA_FILE);
    }

    static size_t load_kv(ScriptHost& host)
    {
        char path[PATH_BUF_LEN];
        size_t len = 0U;
        if (!data_path(host, path, sizeof(path)) ||
            !common::is_ok(read_small_file(*host.m_p_fs, path, s_kv_in, sizeof(s_kv_in), len))) {
            len = 0U;
        }
        return len;
    }

    static int storage_get(lua_State* p_lua)
    {
        require_storage(p_lua);
        const char* const p_key = luaL_checkstring(p_lua, 1);
        const size_t len = load_kv(*host_of(p_lua));
        size_t value_len = 0U;
        const char* const p_value = kv_find(s_kv_in, len, p_key, value_len);
        if (p_value == nullptr) {
            lua_pushnil(p_lua);
        } else {
            lua_pushlstring(p_lua, p_value, value_len);
        }
        return 1;
    }

    static int storage_set(lua_State* p_lua)
    {
        require_storage(p_lua);
        ScriptHost* const p_host = host_of(p_lua);
        const char* const p_key = luaL_checkstring(p_lua, 1);
        size_t value_len = 0U;
        const char* const p_value = luaL_checklstring(p_lua, 2, &value_len);
        luaL_argcheck(p_lua, is_valid_key(p_key), 1, "key must be [A-Za-z0-9_], <= 24 chars");
        luaL_argcheck(p_lua, (value_len <= KV_VALUE_MAX) && (std::memchr(p_value, '\n', value_len) == nullptr), 2,
                      "value too long or contains newline");

        // Ghi lại mọi dòng trừ khóa cũ, rồi thêm khóa mới.
        const size_t in_len = load_kv(*p_host);
        const size_t key_len = std::strlen(p_key);
        size_t out_len = 0U;
        size_t pos = 0U;
        while (pos < in_len) {
            size_t end = pos;
            while ((end < in_len) && (s_kv_in[end] != '\n')) {
                ++end;
            }
            const bool same_key = ((end - pos) > key_len) && (std::memcmp(&s_kv_in[pos], p_key, key_len) == 0) &&
                                  (s_kv_in[pos + key_len] == '=');
            const size_t line_len = end - pos;
            if (!same_key && (line_len > 0U) && ((out_len + line_len + 1U) <= sizeof(s_kv_out))) {
                (void)std::memcpy(&s_kv_out[out_len], &s_kv_in[pos], line_len);
                out_len += line_len;
                s_kv_out[out_len++] = '\n';
            }
            pos = end + 1U;
        }
        const size_t entry_len = key_len + 1U + value_len + 1U;
        if ((out_len + entry_len) > sizeof(s_kv_out)) {
            return luaL_error(p_lua, "storage full (max %d bytes)", static_cast<int>(KV_BUF_LEN));
        }
        (void)std::memcpy(&s_kv_out[out_len], p_key, key_len);
        out_len += key_len;
        s_kv_out[out_len++] = '=';
        (void)std::memcpy(&s_kv_out[out_len], p_value, value_len);
        out_len += value_len;
        s_kv_out[out_len++] = '\n';

        char path[PATH_BUF_LEN];
        hal::FileHandle handle = hal::INVALID_FILE;
        common::Status status = data_path(*p_host, path, sizeof(path))
                                    ? p_host->m_p_fs->open(path, hal::OpenMode::WRITE, handle)
                                    : common::Status::ERR_PARAM;
        if (common::is_ok(status)) {
            status = p_host->m_p_fs->write(handle, reinterpret_cast<const uint8_t*>(s_kv_out), out_len);
            p_host->m_p_fs->close(handle); // Đóng file TRƯỚC mọi luaL_error.
        }
        if (!common::is_ok(status)) {
            return luaL_error(p_lua, "storage write failed");
        }
        return 0;
    }

    // ---- gach.app ----
    static int app_exit(lua_State* p_lua)
    {
        ScriptHost* const p_host = host_of(p_lua);
        if (!p_host->m_exit_pending) {
            p_host->m_exit_pending = true;
            // Không đóng app ngay: máy ảo đang chạy chính lời gọi này.
            (void)lv_async_call(&ScriptHost::async_exit, p_host);
        }
        return 0;
    }

    // ---- toàn cục ----
    static int print(lua_State* p_lua)
    {
        const int count = lua_gettop(p_lua);
        std::printf("[%s]", host_of(p_lua)->m_manifest.id);
        for (int index = 1; index <= count; ++index) {
            std::printf(" %s", luaL_tolstring(p_lua, index, nullptr));
            lua_pop(p_lua, 1);
        }
        std::printf("\n");
        return 0;
    }

    static void add_table(lua_State* p_lua, const char* p_name, const luaL_Reg* p_funcs)
    {
        lua_newtable(p_lua);
        luaL_setfuncs(p_lua, p_funcs, 0);
        lua_setfield(p_lua, -2, p_name);
    }

    /** @brief Chạy trong lua_pcall: mở thư viện an toàn, đăng ký SDK, nạp & chạy entry. */
    static int boot(lua_State* p_lua)
    {
        static const luaL_Reg LIBS[] = {{LUA_GNAME, luaopen_base},          {LUA_TABLIBNAME, luaopen_table},
                                        {LUA_STRLIBNAME, luaopen_string},   {LUA_MATHLIBNAME, luaopen_math},
                                        {LUA_UTF8LIBNAME, luaopen_utf8},    {LUA_COLIBNAME, luaopen_coroutine}};
        for (const luaL_Reg& lib : LIBS) {
            luaL_requiref(p_lua, lib.name, lib.func, 1);
            lua_pop(p_lua, 1);
        }
        // Gỡ hàm nạp code từ file/bytecode khỏi sandbox.
        for (const char* p_name : {"dofile", "loadfile", "load"}) {
            lua_pushnil(p_lua);
            lua_setglobal(p_lua, p_name);
        }
        lua_pushcfunction(p_lua, &Sdk::print);
        lua_setglobal(p_lua, "print");

        static const luaL_Reg UI[] = {{"label", label},       {"button", button},     {"column", column},
                                      {"row", row},           {"list", list},         {"list_add", list_add},
                                      {"slider", slider},     {"set_text", set_text}, {"set_font", set_font},
                                      {"set_color", set_color}, {"clear", clear},     {"delete", destroy},
                                      // SDK v2
                                      {"box", box},           {"set_size", set_size}, {"set_grow", set_grow},
                                      {"set_bg", set_bg},     {"set_radius", set_radius}, {"set_align", set_align},
                                      {"set_pad", set_pad},   {"set_gap", set_gap},   {"center", center},
                                      // SDK v3
                                      {"screen_size", screen_size}, {"on_swipe", on_swipe},
                                      {nullptr, nullptr}};
        static const luaL_Reg TIMER[] = {{"every", timer_every}, {"cancel", timer_cancel}, {nullptr, nullptr}};
        static const luaL_Reg SYS[] = {{"uptime_ms", uptime_ms}, {"time", time}, {nullptr, nullptr}};
        static const luaL_Reg STORAGE[] = {{"get", storage_get}, {"set", storage_set}, {nullptr, nullptr}};
        static const luaL_Reg APP[] = {{"exit", app_exit}, {nullptr, nullptr}};
        lua_newtable(p_lua);
        add_table(p_lua, "ui", UI);
        (void)lua_getfield(p_lua, -1, "ui");
        push_symbols(p_lua);
        lua_setfield(p_lua, -2, "SYMBOL");
        lua_pop(p_lua, 1);
        add_table(p_lua, "timer", TIMER);
        add_table(p_lua, "sys", SYS);
        add_table(p_lua, "storage", STORAGE);
        add_table(p_lua, "app", APP);
        lua_pushinteger(p_lua, SDK_VERSION);
        lua_setfield(p_lua, -2, "sdk_version");
        lua_setglobal(p_lua, "gach");

        // Nạp entry: chỉ chấp nhận mã nguồn văn bản ("t") – bytecode có thể làm crash máy ảo.
        ScriptHost* const p_host = host_of(p_lua);
        LoadContext* const p_ctx = static_cast<LoadContext*>(lua_touserdata(p_lua, 1));
        if (lua_load(p_lua, &file_reader, p_ctx, p_host->m_manifest.entry, "t") != LUA_OK) {
            return lua_error(p_lua);
        }
        lua_call(p_lua, 0, 1);
        if (!lua_istable(p_lua, -1)) {
            return luaL_error(p_lua, "%s must return a table (local app = {} ... return app)",
                              p_host->m_manifest.entry);
        }
        p_host->m_app_ref = luaL_ref(p_lua, LUA_REGISTRYINDEX);
        return 0;
    }
};

// ============================================================================
// ScriptHost
// ============================================================================
ScriptHost::ScriptHost()
    : m_p_fs(nullptr), m_arena{nullptr, 0U}, m_p_lua(nullptr), m_p_apps(nullptr), m_p_services(nullptr),
      m_p_root(nullptr), m_manifest{}, m_objects{}, m_refs{}, m_timers{}, m_app_ref(LUA_NOREF),
      m_load_handle(hal::INVALID_FILE), m_deadline(0U), m_in_call(false), m_faulted(false), m_exit_pending(false),
      m_exiting(false), m_error{}
{
}

void ScriptHost::bind(hal::IFileSystem* p_fs, const hal::MemoryRegion& arena)
{
    m_p_fs = p_fs;
    m_arena = arena;
}

common::Status ScriptHost::start(const Manifest& manifest, lv_obj_t* p_root, fw::AppManager& apps,
                                 fw::Services& services)
{
    if ((m_p_fs == nullptr) || (m_arena.p_base == nullptr)) {
        return common::Status::ERR_NOT_SUPPORTED;
    }
    if (m_p_lua != nullptr) {
        return common::Status::ERR_STATE;
    }
    m_manifest = manifest;
    m_p_root = p_root;
    m_p_apps = &apps;
    m_p_services = &services;
    m_faulted = false;
    m_exit_pending = false;
    m_error[0] = '\0';
    for (size_t index = 0U; index < MAX_OBJECTS; ++index) {
        m_objects[index] = nullptr;
        m_refs[index] = LUA_NOREF;
    }
    for (TimerSlot& slot : m_timers) {
        slot = TimerSlot{nullptr, LUA_NOREF};
    }

    // App sdk >= 3 co giãn theo màn hình; app đời cũ giữ nguyên pixel và font 14 px như lúc phát hành.
    const bool is_scaled_ui = (manifest.sdk >= SDK_SCALED_UI);
    const int32_t root_pad = is_scaled_ui ? fw::theme::pad() : fw::theme::BASE_PAD;
    lv_obj_set_style_pad_all(p_root, root_pad, 0);
    lv_obj_set_style_pad_gap(p_root, root_pad, 0);
    lv_obj_set_flex_flow(p_root, LV_FLEX_FLOW_COLUMN);
    if (!is_scaled_ui) {
        lv_obj_set_style_text_font(p_root, fw::theme::base_font(fw::theme::FontSize::SMALL), 0);
    }

    m_allocator.reset(m_arena.p_base, m_arena.size);
    m_p_lua = lua_newstate(&arena_alloc, &m_allocator);
    if (m_p_lua == nullptr) {
        fault("out of memory (arena too small)");
        return common::Status::OK;
    }
    *static_cast<ScriptHost**>(lua_getextraspace(m_p_lua)) = this;
    lua_sethook(m_p_lua, &ScriptHost::timeout_hook, LUA_MASKCOUNT, HOOK_INSTRUCTION_COUNT);
    (void)register_object(p_root); // Handle 1 = root của app.

    char dir[PATH_BUF_LEN];
    char path[PATH_BUF_LEN];
    LoadContext ctx{m_p_fs, hal::INVALID_FILE, {}};
    common::Status status = (join_path(dir, sizeof(dir), APPS_DIR, manifest.id) &&
                             join_path(path, sizeof(path), dir, manifest.entry))
                                ? m_p_fs->open(path, hal::OpenMode::READ, ctx.handle)
                                : common::Status::ERR_PARAM;
    if (!common::is_ok(status)) {
        fault("cannot open entry script");
        return common::Status::OK;
    }
    m_load_handle = ctx.handle;
    lua_pushcfunction(m_p_lua, &Sdk::boot);
    lua_pushlightuserdata(m_p_lua, &ctx);
    const bool booted = protected_call(1, 0);
    m_p_fs->close(m_load_handle); // Đóng file dù boot lỗi (longjmp đã thoát ra pcall).
    m_load_handle = hal::INVALID_FILE;

    if (booted) {
        const int32_t root = ROOT_HANDLE;
        (void)call_app("on_create", &root, nullptr);
    }
    return common::Status::OK;
}

void ScriptHost::resume()
{
    set_timers_paused(false);
    (void)call_app("on_resume", nullptr, nullptr);
}

void ScriptHost::pause()
{
    (void)call_app("on_pause", nullptr, nullptr);
    set_timers_paused(true);
}

bool ScriptHost::back()
{
    bool handled = false;
    if (!m_exiting) {
        (void)call_app("on_back", nullptr, &handled);
    }
    return handled;
}

void ScriptHost::stop()
{
    if ((m_p_lua != nullptr) && !m_faulted) {
        (void)call_app("on_destroy", nullptr, nullptr);
    }
    close_vm();
    m_exit_pending = false;
    m_p_root = nullptr;
}

int32_t ScriptHost::register_object(lv_obj_t* p_obj)
{
    for (size_t index = 0U; index < MAX_OBJECTS; ++index) {
        if (m_objects[index] == nullptr) {
            m_objects[index] = p_obj;
            m_refs[index] = LUA_NOREF;
            const auto handle = static_cast<int32_t>(index + 1U);
            lv_obj_set_user_data(p_obj, reinterpret_cast<void*>(static_cast<intptr_t>(handle)));
            lv_obj_add_event_cb(p_obj, &ScriptHost::on_lv_event, LV_EVENT_DELETE, this);
            return handle;
        }
    }
    return 0;
}

void ScriptHost::attach_callback(int32_t handle, int ref, lv_event_code_t code)
{
    m_refs[handle - 1] = ref;
    lv_obj_add_event_cb(m_objects[handle - 1], &ScriptHost::on_lv_event, code, this);
}

bool ScriptHost::protected_call(int nargs, int nresults)
{
    m_deadline = osal::tick_ms() + CALL_BUDGET_MS;
    m_in_call = true;
    const int result = lua_pcall(m_p_lua, nargs, nresults, 0);
    m_in_call = false;
    if (result != LUA_OK) {
        const char* const p_msg = lua_tostring(m_p_lua, -1);
        fault((p_msg != nullptr) ? p_msg : "unknown script error");
        lua_pop(m_p_lua, 1);
        return false;
    }
    return true;
}

bool ScriptHost::call_app(const char* p_method, const int32_t* p_arg, bool* p_result)
{
    if ((m_p_lua == nullptr) || m_faulted || m_in_call || (m_app_ref == LUA_NOREF)) {
        return false;
    }
    lua_rawgeti(m_p_lua, LUA_REGISTRYINDEX, m_app_ref);
    (void)lua_getfield(m_p_lua, -1, p_method);
    lua_remove(m_p_lua, -2);
    if (!lua_isfunction(m_p_lua, -1)) {
        lua_pop(m_p_lua, 1);
        return false;
    }
    int nargs = 0;
    if (p_arg != nullptr) {
        lua_pushinteger(m_p_lua, *p_arg);
        nargs = 1;
    }
    if (!protected_call(nargs, 1)) {
        return false;
    }
    if (p_result != nullptr) {
        *p_result = lua_toboolean(m_p_lua, -1) != 0;
    }
    lua_pop(m_p_lua, 1);
    return true;
}

void ScriptHost::invoke(int ref, const int32_t* p_arg)
{
    if ((m_p_lua == nullptr) || m_faulted || m_in_call || (ref == LUA_NOREF)) {
        return;
    }
    lua_rawgeti(m_p_lua, LUA_REGISTRYINDEX, ref);
    int nargs = 0;
    if (p_arg != nullptr) {
        lua_pushinteger(m_p_lua, *p_arg);
        nargs = 1;
    }
    (void)protected_call(nargs, 0);
}

void ScriptHost::fault(const char* p_message)
{
    std::printf("[%s] script error: %s\n", m_manifest.id, p_message);
    if (m_faulted) {
        return;
    }
    m_faulted = true;
    (void)common::copy_string(m_error, sizeof(m_error), p_message);
    set_timers_paused(true);
    // Đóng máy ảo/vẽ lỗi sau: có thể đang ở giữa một lời gọi Lua hoặc sự kiện LVGL.
    (void)lv_async_call(&ScriptHost::async_fault, this);
}

void ScriptHost::close_vm()
{
    for (TimerSlot& slot : m_timers) {
        if (slot.p_timer != nullptr) {
            lv_timer_delete(slot.p_timer);
        }
        slot = TimerSlot{nullptr, LUA_NOREF};
    }
    // Bỏ liên kết handle trước khi đóng VM: sự kiện DELETE đến sau sẽ không chạm vào VM đã đóng.
    for (size_t index = 0U; index < MAX_OBJECTS; ++index) {
        m_objects[index] = nullptr;
        m_refs[index] = LUA_NOREF;
    }
    if (m_p_lua != nullptr) {
        lua_close(m_p_lua);
        m_p_lua = nullptr;
    }
    m_app_ref = LUA_NOREF;
    m_allocator.reset(m_arena.p_base, m_arena.size);
}

void ScriptHost::show_error()
{
    if (m_p_root == nullptr) {
        return;
    }
    lv_obj_clean(m_p_root);
    lv_obj_t* const p_title = lv_label_create(m_p_root);
    lv_label_set_text(p_title, LV_SYMBOL_WARNING " App error");
    lv_obj_set_style_text_color(p_title, lv_color_hex(fw::theme::COLOR_DANGER), 0);
    lv_obj_t* const p_msg = lv_label_create(m_p_root);
    lv_obj_set_width(p_msg, LV_PCT(100));
    lv_label_set_long_mode(p_msg, LV_LABEL_LONG_WRAP);
    lv_label_set_text(p_msg, m_error);
    lv_obj_set_style_text_color(p_msg, lv_color_hex(fw::theme::COLOR_TEXT_MUTED), 0);
}

void ScriptHost::set_timers_paused(bool paused)
{
    for (const TimerSlot& slot : m_timers) {
        if (slot.p_timer != nullptr) {
            if (paused) {
                lv_timer_pause(slot.p_timer);
            } else {
                lv_timer_resume(slot.p_timer);
            }
        }
    }
}

void ScriptHost::on_lv_event(lv_event_t* p_event)
{
    auto* const p_host = static_cast<ScriptHost*>(lv_event_get_user_data(p_event));
    lv_obj_t* const p_obj = lv_event_get_current_target_obj(p_event);
    const auto handle = static_cast<int32_t>(reinterpret_cast<intptr_t>(lv_obj_get_user_data(p_obj)));
    if ((handle < 1) || (handle > static_cast<int32_t>(MAX_OBJECTS)) || (p_host->m_objects[handle - 1] != p_obj)) {
        return; // Object của phiên cũ hoặc đã bị thu hồi.
    }
    const lv_event_code_t code = lv_event_get_code(p_event);
    const int ref = p_host->m_refs[handle - 1];
    if (code == LV_EVENT_DELETE) {
        p_host->m_objects[handle - 1] = nullptr;
        p_host->m_refs[handle - 1] = LUA_NOREF;
        if ((p_host->m_p_lua != nullptr) && (ref != LUA_NOREF)) {
            luaL_unref(p_host->m_p_lua, LUA_REGISTRYINDEX, ref);
        }
    } else if (code == LV_EVENT_VALUE_CHANGED) {
        const int32_t value = lv_slider_get_value(p_obj);
        p_host->invoke(ref, &value);
    } else if (code == LV_EVENT_GESTURE) {
        lv_indev_t* const p_indev = lv_indev_active();
        const char* p_dir = nullptr;
        switch ((p_indev != nullptr) ? lv_indev_get_gesture_dir(p_indev) : LV_DIR_NONE) {
        case LV_DIR_LEFT:
            p_dir = "left";
            break;
        case LV_DIR_RIGHT:
            p_dir = "right";
            break;
        case LV_DIR_TOP:
            p_dir = "up";
            break;
        case LV_DIR_BOTTOM:
            p_dir = "down";
            break;
        default:
            break;
        }
        if (p_dir != nullptr) {
            lv_indev_wait_release(p_indev); // Một lần vuốt = một sự kiện, không lặp khi ngón tay còn trượt.
            p_host->invoke_text(ref, p_dir);
        }
    } else {
        p_host->invoke(ref, nullptr);
    }
}

void ScriptHost::invoke_text(int ref, const char* p_arg)
{
    if ((m_p_lua == nullptr) || m_faulted || m_in_call || (ref == LUA_NOREF)) {
        return;
    }
    lua_rawgeti(m_p_lua, LUA_REGISTRYINDEX, ref);
    lua_pushstring(m_p_lua, p_arg);
    (void)protected_call(1, 0);
}

void ScriptHost::on_timer(lv_timer_t* p_timer)
{
    auto* const p_host = static_cast<ScriptHost*>(lv_timer_get_user_data(p_timer));
    for (const TimerSlot& slot : p_host->m_timers) {
        if (slot.p_timer == p_timer) {
            p_host->invoke(slot.ref, nullptr);
            return;
        }
    }
}

void ScriptHost::async_fault(void* p_arg)
{
    auto* const p_host = static_cast<ScriptHost*>(p_arg);
    if (!p_host->m_faulted || (p_host->m_p_root == nullptr)) {
        return; // App đã bị đóng trước khi callback chạy.
    }
    p_host->close_vm();
    p_host->show_error();
}

void ScriptHost::async_exit(void* p_arg)
{
    auto* const p_host = static_cast<ScriptHost*>(p_arg);
    if (!p_host->m_exit_pending || (p_host->m_p_apps == nullptr)) {
        return;
    }
    p_host->m_exit_pending = false;
    p_host->m_exiting = true; // back() không hỏi lại script.
    p_host->m_p_apps->back();
    p_host->m_exiting = false;
}

void ScriptHost::timeout_hook(lua_State* p_lua, lua_Debug* p_debug)
{
    (void)p_debug;
    ScriptHost* const p_host = host_of(p_lua);
    if (p_host->m_in_call && (static_cast<int32_t>(osal::tick_ms() - p_host->m_deadline) > 0)) {
        (void)luaL_error(p_lua, "script timeout (> %d ms)", static_cast<int>(CALL_BUDGET_MS));
    }
}

} // namespace runtime
