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
#include "framework/list_view.hpp"
#include "framework/services.hpp"
#include "framework/system.hpp"
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

// ---- SDK v4: widget hiển thị số liệu ----
constexpr uint32_t GAUGE_TRACK_COLOR = 0x2C2C2EU;  /**< Nền rãnh bar/arc, lưới chart. */
constexpr uint32_t GAUGE_ACCENT_COLOR = 0x0A84FFU; /**< Màu chỉ thị mặc định. */
constexpr int32_t BAR_DEFAULT_HEIGHT = 8;          /**< dp. */
constexpr int32_t ARC_DEFAULT_SIZE = 120;          /**< dp. */
constexpr int32_t ARC_DEFAULT_THICKNESS = 10;      /**< dp. */
constexpr int32_t ARC_START_ANGLE = 135;           /**< Cung 270°, hở phía dưới. */
constexpr int32_t ARC_END_ANGLE = 45;
constexpr int32_t CHART_DEFAULT_HEIGHT = 120;      /**< dp. */
constexpr int32_t CHART_LINE_WIDTH = 2;            /**< dp. */
constexpr int32_t CHART_MIN_POINTS = 2;
constexpr int32_t CHART_MAX_POINTS = 120;
constexpr int32_t CHART_MAX_SERIES = 4;
constexpr uint32_t CHART_HOR_DIV_LINES = 3U;
constexpr uint32_t MS_PER_S = 1000U;

// ---- SDK v5: mạng & JSON ----
constexpr uint32_t NET_POLL_MS = 50U;
constexpr int JSON_MAX_DEPTH = 32;
constexpr int JSON_STACK_SLOTS = 4;
constexpr uint32_t UTF8_1BYTE_MAX = 0x7FU;
constexpr uint32_t UTF8_2BYTE_MAX = 0x7FFU;
constexpr uint32_t UTF8_3BYTE_MAX = 0xFFFFU;
constexpr uint32_t SURROGATE_HIGH_MIN = 0xD800U;
constexpr uint32_t SURROGATE_HIGH_MAX = 0xDBFFU;
constexpr uint32_t SURROGATE_LOW_MIN = 0xDC00U;
constexpr uint32_t SURROGATE_LOW_MAX = 0xDFFFU;
constexpr uint32_t SURROGATE_BASE = 0x10000U;
constexpr uint32_t SURROGATE_SHIFT = 10U;
constexpr int HEX_DIGITS = 4;

/**
 * @brief Bộ giải mã JSON -> giá trị Lua (object -> table, array -> table 1..n, null -> nil).
 *
 * Đệ quy có giới hạn độ sâu; lỗi trả false kèm vị trí, stack Lua do người gọi dọn.
 */
class JsonDecoder {
public:
    JsonDecoder(lua_State* p_lua, const char* p_text, size_t len)
        : m_p_lua(p_lua), m_p_begin(p_text), m_p_pos(p_text), m_p_end(p_text + len), m_p_error(nullptr)
    {
    }

    bool decode()
    {
        if (!value(0)) {
            return false;
        }
        skip_space();
        return (m_p_pos == m_p_end) || fail("trailing characters");
    }

    const char* error() const { return (m_p_error != nullptr) ? m_p_error : "invalid JSON"; }
    size_t offset() const { return static_cast<size_t>(m_p_pos - m_p_begin); }

private:
    bool fail(const char* p_message)
    {
        m_p_error = p_message;
        return false;
    }

    void skip_space()
    {
        while ((m_p_pos < m_p_end) &&
               ((*m_p_pos == ' ') || (*m_p_pos == '\t') || (*m_p_pos == '\n') || (*m_p_pos == '\r'))) {
            ++m_p_pos;
        }
    }

    bool literal(const char* p_word)
    {
        const size_t len = std::strlen(p_word);
        if ((static_cast<size_t>(m_p_end - m_p_pos) < len) || (std::memcmp(m_p_pos, p_word, len) != 0)) {
            return fail("invalid literal");
        }
        m_p_pos += len;
        return true;
    }

    bool value(int depth)
    {
        if ((depth > JSON_MAX_DEPTH) || !lua_checkstack(m_p_lua, JSON_STACK_SLOTS)) {
            return fail("nesting too deep");
        }
        skip_space();
        if (m_p_pos >= m_p_end) {
            return fail("unexpected end");
        }
        switch (*m_p_pos) {
        case '{':
            return object(depth);
        case '[':
            return array(depth);
        case '"':
            return string();
        case 't':
            lua_pushboolean(m_p_lua, 1);
            return literal("true");
        case 'f':
            lua_pushboolean(m_p_lua, 0);
            return literal("false");
        case 'n':
            lua_pushnil(m_p_lua);
            return literal("null");
        default:
            return number();
        }
    }

    bool object(int depth)
    {
        ++m_p_pos; // '{'
        lua_newtable(m_p_lua);
        skip_space();
        if ((m_p_pos < m_p_end) && (*m_p_pos == '}')) {
            ++m_p_pos;
            return true;
        }
        for (;;) {
            skip_space();
            if ((m_p_pos >= m_p_end) || (*m_p_pos != '"') || !string()) {
                return fail("expected key");
            }
            skip_space();
            if ((m_p_pos >= m_p_end) || (*m_p_pos != ':')) {
                return fail("expected ':'");
            }
            ++m_p_pos;
            if (!value(depth + 1)) {
                return false;
            }
            lua_rawset(m_p_lua, -3);
            skip_space();
            if ((m_p_pos < m_p_end) && (*m_p_pos == ',')) {
                ++m_p_pos;
            } else if ((m_p_pos < m_p_end) && (*m_p_pos == '}')) {
                ++m_p_pos;
                return true;
            } else {
                return fail("expected ',' or '}'");
            }
        }
    }

    bool array(int depth)
    {
        ++m_p_pos; // '['
        lua_newtable(m_p_lua);
        skip_space();
        if ((m_p_pos < m_p_end) && (*m_p_pos == ']')) {
            ++m_p_pos;
            return true;
        }
        for (lua_Integer index = 1;; ++index) {
            if (!value(depth + 1)) {
                return false;
            }
            lua_rawseti(m_p_lua, -2, index);
            skip_space();
            if ((m_p_pos < m_p_end) && (*m_p_pos == ',')) {
                ++m_p_pos;
            } else if ((m_p_pos < m_p_end) && (*m_p_pos == ']')) {
                ++m_p_pos;
                return true;
            } else {
                return fail("expected ',' or ']'");
            }
        }
    }

    bool hex4(uint32_t& out)
    {
        if ((m_p_end - m_p_pos) < HEX_DIGITS) {
            return fail("bad \\u escape");
        }
        out = 0U;
        for (int i = 0; i < HEX_DIGITS; ++i) {
            const char c = *m_p_pos++;
            uint32_t digit = 0U;
            if ((c >= '0') && (c <= '9')) {
                digit = static_cast<uint32_t>(c - '0');
            } else if ((c >= 'a') && (c <= 'f')) {
                digit = static_cast<uint32_t>(c - 'a') + 10U;
            } else if ((c >= 'A') && (c <= 'F')) {
                digit = static_cast<uint32_t>(c - 'A') + 10U;
            } else {
                return fail("bad \\u escape");
            }
            out = (out << 4U) | digit;
        }
        return true;
    }

    static void add_utf8(luaL_Buffer& buf, uint32_t cp)
    {
        if (cp <= UTF8_1BYTE_MAX) {
            luaL_addchar(&buf, static_cast<char>(cp));
        } else if (cp <= UTF8_2BYTE_MAX) {
            luaL_addchar(&buf, static_cast<char>(0xC0U | (cp >> 6U)));
            luaL_addchar(&buf, static_cast<char>(0x80U | (cp & 0x3FU)));
        } else if (cp <= UTF8_3BYTE_MAX) {
            luaL_addchar(&buf, static_cast<char>(0xE0U | (cp >> 12U)));
            luaL_addchar(&buf, static_cast<char>(0x80U | ((cp >> 6U) & 0x3FU)));
            luaL_addchar(&buf, static_cast<char>(0x80U | (cp & 0x3FU)));
        } else {
            luaL_addchar(&buf, static_cast<char>(0xF0U | (cp >> 18U)));
            luaL_addchar(&buf, static_cast<char>(0x80U | ((cp >> 12U) & 0x3FU)));
            luaL_addchar(&buf, static_cast<char>(0x80U | ((cp >> 6U) & 0x3FU)));
            luaL_addchar(&buf, static_cast<char>(0x80U | (cp & 0x3FU)));
        }
    }

    bool escape(luaL_Buffer& buf)
    {
        const char c = *m_p_pos++;
        switch (c) {
        case '"':
        case '\\':
        case '/':
            luaL_addchar(&buf, c);
            return true;
        case 'b':
            luaL_addchar(&buf, '\b');
            return true;
        case 'f':
            luaL_addchar(&buf, '\f');
            return true;
        case 'n':
            luaL_addchar(&buf, '\n');
            return true;
        case 'r':
            luaL_addchar(&buf, '\r');
            return true;
        case 't':
            luaL_addchar(&buf, '\t');
            return true;
        case 'u':
            break;
        default:
            return fail("bad escape");
        }
        uint32_t cp = 0U;
        if (!hex4(cp)) {
            return false;
        }
        if ((cp >= SURROGATE_HIGH_MIN) && (cp <= SURROGATE_HIGH_MAX) && ((m_p_end - m_p_pos) >= 2) &&
            (m_p_pos[0] == '\\') && (m_p_pos[1] == 'u')) {
            m_p_pos += 2;
            uint32_t low = 0U;
            if (!hex4(low)) {
                return false;
            }
            if ((low >= SURROGATE_LOW_MIN) && (low <= SURROGATE_LOW_MAX)) {
                cp = SURROGATE_BASE + ((cp - SURROGATE_HIGH_MIN) << SURROGATE_SHIFT) + (low - SURROGATE_LOW_MIN);
            }
        }
        add_utf8(buf, cp);
        return true;
    }

    bool string()
    {
        ++m_p_pos; // '"'
        luaL_Buffer buf;
        luaL_buffinit(m_p_lua, &buf);
        while (m_p_pos < m_p_end) {
            const char c = *m_p_pos++;
            if (c == '"') {
                luaL_pushresult(&buf);
                return true;
            }
            if (c != '\\') {
                luaL_addchar(&buf, c);
            } else if ((m_p_pos >= m_p_end) || !escape(buf)) {
                return fail("bad escape");
            } else {
                // Đã thêm ký tự thoát.
            }
        }
        return fail("unterminated string");
    }

    bool number()
    {
        const char* const p_start = m_p_pos;
        bool is_integer = true;
        while ((m_p_pos < m_p_end) && (std::strchr("+-0123456789.eE", *m_p_pos) != nullptr)) {
            if ((*m_p_pos == '.') || (*m_p_pos == 'e') || (*m_p_pos == 'E')) {
                is_integer = false;
            }
            ++m_p_pos;
        }
        if (m_p_pos == p_start) {
            return fail("unexpected character");
        }
        char* p_stop = nullptr;
        const double value = std::strtod(p_start, &p_stop); // Chuỗi Lua luôn kết thúc '\0'.
        if (p_stop != m_p_pos) {
            return fail("bad number");
        }
        if (is_integer && (value >= static_cast<double>(INT32_MIN)) && (value <= static_cast<double>(INT32_MAX))) {
            lua_pushinteger(m_p_lua, static_cast<lua_Integer>(value));
        } else {
            lua_pushnumber(m_p_lua, static_cast<lua_Number>(value));
        }
        return true;
    }

    lua_State* const m_p_lua;
    const char* const m_p_begin;
    const char* m_p_pos;
    const char* const m_p_end;
    const char* m_p_error;
};

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
        lv_obj_set_clickable(p_box, false);
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
        lv_obj_t* const p_list = fw::list_view::create(object(p_lua, 1)); // Rộng 100%, chiếm phần còn lại.
        return push_new(p_lua, p_list, 0, LV_EVENT_ALL);
    }

    static int list_add(lua_State* p_lua)
    {
        lv_obj_t* const p_list = object(p_lua, 1);
        const char* const p_text = luaL_checkstring(p_lua, 2);
        return push_new(p_lua, fw::list_view::add(p_list, nullptr, p_text), 3, LV_EVENT_CLICKED);
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
        lv_obj_set_clickable(p_obj, true);
        lv_obj_set_scrollable(p_obj, false);
        // LVGL chuyển cử chỉ lên cha chừng nào còn GESTURE_BUBBLE (mặc định bật cho mọi object):
        // tắt ở đây để cử chỉ dừng tại widget này thay vì trôi lên khung shell.
        lv_obj_set_gesture_bubble(p_obj, false);
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
        lv_obj_set_clickable(p_box, false); // Hình trang trí: click rơi xuống widget cha.
        lv_obj_set_scrollable(p_box, false);
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

    /**
     * @brief Thời gian chờ thẻ SD là của hệ thống, không phải của script: cộng bù vào hạn 500 ms của lần gọi.
     *        (Ghi FAT + đóng file có thể mất vài trăm ms -> app gọi storage vài lần sẽ bị "script timeout" oan.)
     */
    static void credit_io_time(ScriptHost& host, uint32_t start_ms) { host.m_deadline += osal::tick_ms() - start_ms; }

    static int storage_get(lua_State* p_lua)
    {
        require_storage(p_lua);
        const char* const p_key = luaL_checkstring(p_lua, 1);
        const uint32_t io_start = osal::tick_ms();
        const size_t len = load_kv(*host_of(p_lua));
        credit_io_time(*host_of(p_lua), io_start);
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
        const uint32_t io_start = osal::tick_ms();
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
        credit_io_time(*p_host, io_start);
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

    // ---- gach.ui (SDK v4: widget số liệu) ----
    static int bar(lua_State* p_lua)
    {
        lv_obj_t* const p_parent = object(p_lua, 1);
        const auto min = static_cast<int32_t>(luaL_checkinteger(p_lua, 2));
        const auto max = static_cast<int32_t>(luaL_checkinteger(p_lua, 3));
        luaL_argcheck(p_lua, min < max, 3, "max must be greater than min");
        lv_obj_t* const p_bar = lv_bar_create(p_parent);
        lv_obj_remove_style_all(p_bar);
        lv_obj_set_size(p_bar, LV_PCT(100), fw::theme::px(BAR_DEFAULT_HEIGHT));
        lv_obj_set_style_bg_color(p_bar, lv_color_hex(GAUGE_TRACK_COLOR), LV_PART_MAIN);
        lv_obj_set_style_bg_opa(p_bar, LV_OPA_COVER, LV_PART_MAIN);
        lv_obj_set_style_radius(p_bar, LV_RADIUS_CIRCLE, LV_PART_MAIN);
        lv_obj_set_style_bg_color(p_bar, lv_color_hex(GAUGE_ACCENT_COLOR), LV_PART_INDICATOR);
        lv_obj_set_style_bg_opa(p_bar, LV_OPA_COVER, LV_PART_INDICATOR);
        lv_obj_set_style_radius(p_bar, LV_RADIUS_CIRCLE, LV_PART_INDICATOR);
        lv_bar_set_range(p_bar, min, max);
        lv_bar_set_value(p_bar, static_cast<int32_t>(luaL_optinteger(p_lua, 4, min)), LV_ANIM_OFF);
        return push_new(p_lua, p_bar, 0, LV_EVENT_ALL);
    }

    static int arc(lua_State* p_lua)
    {
        lv_obj_t* const p_parent = object(p_lua, 1);
        const auto min = static_cast<int32_t>(luaL_checkinteger(p_lua, 2));
        const auto max = static_cast<int32_t>(luaL_checkinteger(p_lua, 3));
        luaL_argcheck(p_lua, min < max, 3, "max must be greater than min");
        const int32_t thickness = to_px(p_lua, luaL_optinteger(p_lua, 4, ARC_DEFAULT_THICKNESS));
        const int32_t size = fw::theme::px(ARC_DEFAULT_SIZE);
        lv_obj_t* const p_arc = lv_arc_create(p_parent);
        lv_obj_remove_style_all(p_arc); // Bỏ cả núm kéo: chỉ để hiển thị.
        lv_obj_set_size(p_arc, size, size);
        lv_obj_set_clickable(p_arc, false);
        for (const lv_part_t part : {LV_PART_MAIN, LV_PART_INDICATOR}) {
            lv_obj_set_style_arc_width(p_arc, thickness, part);
            lv_obj_set_style_arc_rounded(p_arc, true, part);
            lv_obj_set_style_arc_opa(p_arc, LV_OPA_COVER, part);
        }
        lv_obj_set_style_arc_color(p_arc, lv_color_hex(GAUGE_TRACK_COLOR), LV_PART_MAIN);
        lv_obj_set_style_arc_color(p_arc, lv_color_hex(GAUGE_ACCENT_COLOR), LV_PART_INDICATOR);
        lv_arc_set_bg_angles(p_arc, ARC_START_ANGLE, ARC_END_ANGLE);
        lv_arc_set_range(p_arc, min, max);
        lv_arc_set_value(p_arc, min);
        return push_new(p_lua, p_arc, 0, LV_EVENT_ALL);
    }

    static int chart(lua_State* p_lua)
    {
        lv_obj_t* const p_parent = object(p_lua, 1);
        const auto points = static_cast<int32_t>(luaL_checkinteger(p_lua, 2));
        const auto min = static_cast<int32_t>(luaL_checkinteger(p_lua, 3));
        const auto max = static_cast<int32_t>(luaL_checkinteger(p_lua, 4));
        luaL_argcheck(p_lua, (points >= CHART_MIN_POINTS) && (points <= CHART_MAX_POINTS), 2, "points must be 2..120");
        luaL_argcheck(p_lua, min < max, 4, "max must be greater than min");
        lv_obj_t* const p_chart = lv_chart_create(p_parent);
        lv_obj_remove_style_all(p_chart);
        lv_obj_set_size(p_chart, LV_PCT(100), fw::theme::px(CHART_DEFAULT_HEIGHT));
        lv_obj_set_clickable(p_chart, false);
        lv_obj_set_style_line_color(p_chart, lv_color_hex(GAUGE_TRACK_COLOR), LV_PART_MAIN); // Lưới.
        lv_obj_set_style_line_width(p_chart, 1, LV_PART_MAIN);
        lv_obj_set_style_line_opa(p_chart, LV_OPA_COVER, LV_PART_MAIN);
        lv_obj_set_style_line_width(p_chart, fw::theme::px(CHART_LINE_WIDTH), LV_PART_ITEMS);
        lv_obj_set_style_line_rounded(p_chart, true, LV_PART_ITEMS);
        lv_obj_set_style_width(p_chart, 0, LV_PART_INDICATOR); // Không vẽ chấm tại điểm.
        lv_obj_set_style_height(p_chart, 0, LV_PART_INDICATOR);
        lv_chart_set_type(p_chart, LV_CHART_TYPE_LINE);
        lv_chart_set_update_mode(p_chart, LV_CHART_UPDATE_MODE_SHIFT);
        lv_chart_set_point_count(p_chart, static_cast<uint32_t>(points));
        lv_chart_set_axis_range(p_chart, LV_CHART_AXIS_PRIMARY_Y, min, max);
        lv_chart_set_div_line_count(p_chart, CHART_HOR_DIV_LINES, 0U);
        return push_new(p_lua, p_chart, 0, LV_EVENT_ALL);
    }

    static lv_obj_t* chart_object(lua_State* p_lua, int arg)
    {
        lv_obj_t* const p_chart = object(p_lua, arg);
        luaL_argcheck(p_lua, lv_obj_check_type(p_chart, &lv_chart_class), arg, "not a chart");
        return p_chart;
    }

    static int chart_series(lua_State* p_lua)
    {
        lv_obj_t* const p_chart = chart_object(p_lua, 1);
        const auto color = static_cast<uint32_t>(luaL_optinteger(p_lua, 2, GAUGE_ACCENT_COLOR));
        int32_t count = 0;
        for (lv_chart_series_t* p_ser = lv_chart_get_series_next(p_chart, nullptr); p_ser != nullptr;
             p_ser = lv_chart_get_series_next(p_chart, p_ser)) {
            ++count;
        }
        luaL_argcheck(p_lua, count < CHART_MAX_SERIES, 1, "too many series (max 4)");
        if (lv_chart_add_series(p_chart, lv_color_hex(color), LV_CHART_AXIS_PRIMARY_Y) == nullptr) {
            return luaL_error(p_lua, "out of UI memory");
        }
        lua_pushinteger(p_lua, count + 1);
        return 1;
    }

    static int chart_push(lua_State* p_lua)
    {
        lv_obj_t* const p_chart = chart_object(p_lua, 1);
        const lua_Integer index = luaL_checkinteger(p_lua, 2);
        const auto value = static_cast<int32_t>(luaL_checknumber(p_lua, 3));
        lv_chart_series_t* p_ser = lv_chart_get_series_next(p_chart, nullptr);
        for (lua_Integer i = 1; (p_ser != nullptr) && (i < index); ++i) {
            p_ser = lv_chart_get_series_next(p_chart, p_ser);
        }
        luaL_argcheck(p_lua, (index >= 1) && (p_ser != nullptr), 2, "invalid series");
        lv_chart_set_next_value(p_chart, p_ser, value);
        return 0;
    }

    static int set_value(lua_State* p_lua)
    {
        lv_obj_t* const p_obj = object(p_lua, 1);
        const auto value = static_cast<int32_t>(luaL_checknumber(p_lua, 2));
        if (lv_obj_check_type(p_obj, &lv_arc_class)) {
            lv_arc_set_value(p_obj, value);
        } else if (lv_obj_check_type(p_obj, &lv_slider_class)) {
            lv_slider_set_value(p_obj, value, LV_ANIM_OFF);
        } else if (lv_obj_check_type(p_obj, &lv_bar_class)) {
            lv_bar_set_value(p_obj, value, LV_ANIM_OFF);
        } else {
            return luaL_argerror(p_lua, 1, "expected bar, arc or slider");
        }
        return 0;
    }

    static int set_accent(lua_State* p_lua)
    {
        lv_obj_t* const p_obj = object(p_lua, 1);
        const lv_color_t color = lv_color_hex(static_cast<uint32_t>(luaL_checkinteger(p_lua, 2)));
        if (lv_obj_check_type(p_obj, &lv_arc_class)) {
            lv_obj_set_style_arc_color(p_obj, color, LV_PART_INDICATOR);
        } else {
            lv_obj_set_style_bg_color(p_obj, color, LV_PART_INDICATOR); // bar, slider.
        }
        return 0;
    }

    // ---- gach.sys.stats (SDK v4) ----
    /** @brief Số byte: integer nếu vừa 32 bit (Lua build số nguyên 32 bit), không thì số thực. */
    static void push_bytes(lua_State* p_lua, const char* p_key, uint64_t bytes)
    {
        if (bytes <= static_cast<uint64_t>(INT32_MAX)) {
            lua_pushinteger(p_lua, static_cast<lua_Integer>(bytes));
        } else {
            lua_pushnumber(p_lua, static_cast<lua_Number>(bytes));
        }
        lua_setfield(p_lua, -2, p_key);
    }

    static void push_int(lua_State* p_lua, const char* p_key, int32_t value)
    {
        lua_pushinteger(p_lua, static_cast<lua_Integer>(value));
        lua_setfield(p_lua, -2, p_key);
    }

    static void push_memory(lua_State* p_lua, const fw::SystemStats& st)
    {
        lua_createtable(p_lua, 0, 4);
        push_bytes(p_lua, "total", st.ram_total);
        push_bytes(p_lua, "free", st.ram_free);
        push_bytes(p_lua, "min", st.ram_min);
        push_bytes(p_lua, "largest", st.ram_largest);
        lua_setfield(p_lua, -2, "ram");
        if (st.psram_total > 0U) {
            lua_createtable(p_lua, 0, 3);
            push_bytes(p_lua, "total", st.psram_total);
            push_bytes(p_lua, "free", st.psram_free);
            push_bytes(p_lua, "min", st.psram_min);
            lua_setfield(p_lua, -2, "psram");
        }
        lv_mem_monitor_t mon{};
        lv_mem_monitor(&mon);
        lua_createtable(p_lua, 0, 4);
        push_bytes(p_lua, "total", mon.total_size);
        push_bytes(p_lua, "free", mon.free_size);
        push_int(p_lua, "used_pct", mon.used_pct);
        push_int(p_lua, "frag_pct", mon.frag_pct);
        lua_setfield(p_lua, -2, "lvgl");
    }

    static void push_cpu(lua_State* p_lua, const fw::SystemStats& st)
    {
        lua_createtable(p_lua, 0, 4);
        push_int(p_lua, "cores", st.cpu_cores);
        push_int(p_lua, "mhz", static_cast<int32_t>(st.cpu_freq_mhz));
        if (st.has_cpu) {
            push_int(p_lua, "tasks", st.task_count);
            lua_createtable(p_lua, st.cpu_cores, 0);
            for (uint8_t core = 0U; core < st.cpu_cores; ++core) {
                lua_pushinteger(p_lua, st.cpu_load_pct[core]);
                lua_rawseti(p_lua, -2, core + 1);
            }
            lua_setfield(p_lua, -2, "load");
        }
        lua_setfield(p_lua, -2, "cpu");
    }

    static void push_storage(lua_State* p_lua, const fw::SystemStats& st)
    {
        if (st.has_firmware) {
            lua_createtable(p_lua, 0, 3);
            push_bytes(p_lua, "total", st.flash_total);
            push_bytes(p_lua, "app_size", st.app_partition);
            push_bytes(p_lua, "app_used", st.app_used);
            lua_setfield(p_lua, -2, "flash");
        }
        if (st.has_sd) {
            lua_createtable(p_lua, 0, 2);
            push_bytes(p_lua, "total", st.sd_total);
            push_bytes(p_lua, "free", st.sd_free);
            lua_setfield(p_lua, -2, "sd");
        }
    }

    static void push_wifi(lua_State* p_lua, const fw::Services& services, const fw::SystemStats& st)
    {
        const fw::WifiService& wifi = services.wifi();
        if (!wifi.is_available()) {
            return;
        }
        lua_createtable(p_lua, 0, 4);
        const bool connected = (wifi.status() == fw::WifiStatus::CONNECTED);
        lua_pushboolean(p_lua, connected ? 1 : 0);
        lua_setfield(p_lua, -2, "connected");
        lua_pushstring(p_lua, wifi.current_ssid());
        lua_setfield(p_lua, -2, "ssid");
        char ip[fw::IP_ADDRESS_BUF_LEN] = {};
        if (wifi.ip_address(ip, sizeof(ip))) {
            lua_pushstring(p_lua, ip);
            lua_setfield(p_lua, -2, "ip");
        }
        if (st.has_wifi_rssi) {
            push_int(p_lua, "rssi", st.wifi_rssi);
        }
        lua_setfield(p_lua, -2, "wifi");
    }

    /** @return Số lần vẽ lại mỗi giây kể từ lần gọi trước (cùng bộ đếm với lớp phủ hiệu năng). */
    static lua_Number redraw_rate()
    {
        static uint32_t s_prev_frames = 0U;
        static uint32_t s_prev_ms = 0U;
        fw::PerfStats perf{};
        fw::perf_snapshot(perf, false);
        const uint32_t now = osal::tick_ms();
        const uint32_t elapsed = now - s_prev_ms;
        // Bộ đếm bị reset (gach perf reset) -> chỉ lấy mốc mới.
        const uint32_t frames = (perf.frames >= s_prev_frames) ? (perf.frames - s_prev_frames) : 0U;
        const bool valid = (s_prev_ms != 0U) && (elapsed > 0U);
        s_prev_frames = perf.frames;
        s_prev_ms = now;
        return valid ? ((static_cast<lua_Number>(frames) * MS_PER_S) / elapsed) : 0.0;
    }

    static int stats(lua_State* p_lua)
    {
        ScriptHost* const p_host = host_of(p_lua);
        const uint32_t io_start = osal::tick_ms();
        fw::SystemStats st{};
        p_host->m_p_services->system_stats(st); // Có thể đọc thẻ SD -> không tính vào ngân sách script.
        credit_io_time(*p_host, io_start);

        fw::DeviceInfo info{};
        p_host->m_p_services->device_info(info);

        lua_newtable(p_lua);
        lua_pushstring(p_lua, (info.p_chip_name != nullptr) ? info.p_chip_name : "");
        lua_setfield(p_lua, -2, "chip");
        lua_pushstring(p_lua, (info.p_board_name != nullptr) ? info.p_board_name : "");
        lua_setfield(p_lua, -2, "board");
        push_int(p_lua, "uptime_ms", static_cast<int32_t>(p_host->m_p_services->uptime_ms()));
        if (st.has_memory) {
            push_memory(p_lua, st);
        }
        push_cpu(p_lua, st);
        push_storage(p_lua, st);
        if (st.has_temperature) {
            lua_pushnumber(p_lua, static_cast<lua_Number>(st.temperature_c));
            lua_setfield(p_lua, -2, "temp");
        }
        lua_pushnumber(p_lua, redraw_rate());
        lua_setfield(p_lua, -2, "fps");
        push_wifi(p_lua, *p_host->m_p_services, st);
        return 1;
    }

    /** @brief gach.ui.set_pos(h, x, y): vị trí tuyệt đối (dp) trong cha KHÔNG có bố cục (box) – SDK v5. */
    static int set_pos(lua_State* p_lua)
    {
        lv_obj_t* const p_obj = object(p_lua, 1);
        lv_obj_set_pos(p_obj, to_px(p_lua, luaL_checkinteger(p_lua, 2)), to_px(p_lua, luaL_checkinteger(p_lua, 3)));
        return 0;
    }

    // ---- gach.net / gach.json (SDK v5) ----
    /**
     * @brief gach.net.get(url, fn) -> true | nil, lý do.
     *
     * fn(code, body, truncated) chạy trên UI khi xong: code = mã HTTP, hoặc 0 khi lỗi mạng (body = lý do).
     */
    static int net_get(lua_State* p_lua)
    {
        ScriptHost* const p_host = host_of(p_lua);
        if ((p_host->m_manifest.permissions & PERMISSION_NETWORK) == 0U) {
            return luaL_error(p_lua, "permission 'network' not declared in manifest");
        }
        const char* const p_url = luaL_checkstring(p_lua, 1);
        luaL_checktype(p_lua, 2, LUA_TFUNCTION);
        hal::IHttpClient* const p_http = p_host->m_p_http;
        const char* p_reason = nullptr;
        if (p_http == nullptr) {
            p_reason = "not supported";
        } else if (p_host->m_p_services->wifi().status() != fw::WifiStatus::CONNECTED) {
            p_reason = "offline";
        } else if (p_host->m_net_ref != LUA_NOREF) {
            p_reason = "busy";
        } else {
            p_http->release(); // Kết quả mồ côi của app trước (đã đóng khi yêu cầu còn chạy).
            const common::Status status = p_http->start_get(p_url);
            luaL_argcheck(p_lua, status != common::Status::ERR_PARAM, 1, "url empty or too long");
            if (status != common::Status::OK) {
                p_reason = "busy";
            }
        }
        if (p_reason != nullptr) {
            lua_pushnil(p_lua);
            lua_pushstring(p_lua, p_reason);
            return 2;
        }
        lua_pushvalue(p_lua, 2);
        p_host->m_net_ref = luaL_ref(p_lua, LUA_REGISTRYINDEX);
        p_host->m_p_net_timer = lv_timer_create(&ScriptHost::on_net_poll, NET_POLL_MS, p_host);
        lua_pushboolean(p_lua, 1);
        return 1;
    }

    /** @brief gach.json.decode(text) -> value | nil, "lỗi tại vị trí n". */
    static int json_decode(lua_State* p_lua)
    {
        size_t len = 0U;
        const char* const p_text = luaL_checklstring(p_lua, 1, &len);
        const int top = lua_gettop(p_lua);
        JsonDecoder decoder(p_lua, p_text, len);
        if (decoder.decode()) {
            return 1;
        }
        lua_settop(p_lua, top);
        lua_pushnil(p_lua);
        lua_pushfstring(p_lua, "%s at offset %d", decoder.error(), static_cast<int>(decoder.offset()));
        return 2;
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
                                      // SDK v4
                                      {"bar", bar},           {"arc", arc},           {"chart", chart},
                                      {"chart_series", chart_series}, {"chart_push", chart_push},
                                      {"set_value", set_value}, {"set_accent", set_accent},
                                      // SDK v5
                                      {"set_pos", set_pos},
                                      {nullptr, nullptr}};
        static const luaL_Reg TIMER[] = {{"every", timer_every}, {"cancel", timer_cancel}, {nullptr, nullptr}};
        static const luaL_Reg SYS[] = {{"uptime_ms", uptime_ms}, {"time", time}, {"stats", stats}, {nullptr, nullptr}};
        static const luaL_Reg STORAGE[] = {{"get", storage_get}, {"set", storage_set}, {nullptr, nullptr}};
        static const luaL_Reg APP[] = {{"exit", app_exit}, {nullptr, nullptr}};
        static const luaL_Reg NET[] = {{"get", net_get}, {nullptr, nullptr}};
        static const luaL_Reg JSON[] = {{"decode", json_decode}, {nullptr, nullptr}};
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
        add_table(p_lua, "net", NET);
        add_table(p_lua, "json", JSON);
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
      m_p_root(nullptr), m_manifest{}, m_objects{}, m_refs{}, m_timers{}, m_p_http(nullptr), m_p_net_timer(nullptr),
      m_net_ref(LUA_NOREF), m_app_ref(LUA_NOREF),
      m_load_handle(hal::INVALID_FILE), m_deadline(0U), m_in_call(false), m_faulted(false), m_exit_pending(false),
      m_exiting(false), m_error{}
{
}

void ScriptHost::bind(hal::IFileSystem* p_fs, const hal::MemoryRegion& arena, hal::IHttpClient* p_http)
{
    m_p_fs = p_fs;
    m_arena = arena;
    m_p_http = p_http;
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
    // Yêu cầu HTTP đang chạy vẫn tiếp tục ở platform; kết quả bị bỏ ở lần gach.net.get kế tiếp.
    if (m_p_net_timer != nullptr) {
        lv_timer_delete(m_p_net_timer);
        m_p_net_timer = nullptr;
    }
    m_net_ref = LUA_NOREF;
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

void ScriptHost::on_net_poll(lv_timer_t* p_timer)
{
    auto* const p_self = static_cast<ScriptHost*>(lv_timer_get_user_data(p_timer));
    hal::IHttpClient* const p_http = p_self->m_p_http;
    if ((p_http == nullptr) || (p_http->state() != hal::HttpState::DONE) || p_self->m_in_call) {
        return;
    }
    lv_timer_delete(p_timer);
    p_self->m_p_net_timer = nullptr;
    const int ref = p_self->m_net_ref;
    p_self->m_net_ref = LUA_NOREF;
    if ((p_self->m_p_lua == nullptr) || p_self->m_faulted || (ref == LUA_NOREF)) {
        p_http->release();
        return;
    }
    lua_State* const p_lua = p_self->m_p_lua;
    const hal::HttpResult result = p_http->result();
    lua_rawgeti(p_lua, LUA_REGISTRYINDEX, ref);
    luaL_unref(p_lua, LUA_REGISTRYINDEX, ref);
    if (result.status == common::Status::OK) {
        lua_pushinteger(p_lua, static_cast<lua_Integer>(result.http_code));
        lua_pushlstring(p_lua, reinterpret_cast<const char*>(p_http->body()), result.body_len); // Copy vào arena.
        lua_pushboolean(p_lua, result.truncated ? 1 : 0);
    } else {
        lua_pushinteger(p_lua, 0);
        lua_pushstring(p_lua, (result.status == common::Status::ERR_TIMEOUT) ? "timeout" : "network error");
        lua_pushboolean(p_lua, 0);
    }
    p_http->release();
    (void)p_self->protected_call(3, 0);
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
