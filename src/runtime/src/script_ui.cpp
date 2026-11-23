/**
 * @file    script_ui.cpp
 * @brief   gach SDK v6: sự kiện đa dạng (ui.on), nút nhiều kiểu, widget nhập liệu, giao diện hệ thống
 *          (alert, toast, bàn phím), kiểu dáng gom một lệnh (ui.style) và hiệu ứng (ui.animate).
 *
 * Tách khỏi script_host.cpp cho gọn; dùng chung bảng handle/callback của ScriptHost qua các hàm cầu nối.
 * Mọi bộ nhớ động đi qua arena Lua (chuỗi, registry ref) hoặc heap LVGL – không cấp phát trực tiếp.
 */
#include <cstring>

#include "framework/system_ui.hpp"
#include "framework/theme.hpp"
#include "script_host.hpp"

namespace runtime {

namespace {

// ---- Loại sự kiện của gach.ui.on ----
enum EventKind : uint8_t {
    EV_CLICK = 0U,
    EV_LONG_PRESS,
    EV_PRESS,
    EV_RELEASE,
    EV_CHANGE,
    EV_FOCUS,
    EV_BLUR,
    EV_SUBMIT,
    EV_SWIPE,
    EV_COUNT
};

const char* const EVENT_NAMES[] = {"click", "long_press", "press", "release", "change",
                                   "focus", "blur",       "submit", "swipe",   nullptr};
const lv_event_code_t EVENT_CODES[EV_COUNT] = {
    LV_EVENT_CLICKED, LV_EVENT_LONG_PRESSED, LV_EVENT_PRESSED, LV_EVENT_RELEASED, LV_EVENT_VALUE_CHANGED,
    LV_EVENT_FOCUSED, LV_EVENT_DEFOCUSED,    LV_EVENT_READY,   LV_EVENT_GESTURE};

// ---- Loại widget tự dựng (ScriptHost::m_widget_kind) ----
enum WidgetKind : uint8_t {
    KIND_NONE = 0U,
    KIND_SEGMENTED,
    KIND_TEXTFIELD,
    KIND_DROPDOWN
};

// ---- Kích thước (dp) ----
constexpr int32_t BTN_HEIGHT_SM = 32;
constexpr int32_t BTN_HEIGHT_MD = 40;
constexpr int32_t BTN_HEIGHT_LG = 52;
constexpr int32_t BTN_PAD_SM = 12;
constexpr int32_t BTN_PAD_MD = 18;
constexpr int32_t BTN_PAD_LG = 24;
constexpr int32_t BTN_RADIUS = 10;
constexpr int32_t BTN_BORDER = 2;
constexpr lv_opa_t BTN_TINT_OPA = 56;       /**< ~22 %: nền nhạt của nút "tinted". */
constexpr lv_opa_t PRESSED_OPA = LV_OPA_70; /**< Phản hồi nhấn chung cho nút v6. */
constexpr lv_opa_t DISABLED_OPA = 90;       /**< ~35 %. */
constexpr int32_t SWITCH_W = 51;
constexpr int32_t SWITCH_H = 31;
constexpr int32_t CHECK_RADIUS = 6;
constexpr int32_t CHECK_GAP = 10;
constexpr int32_t SEG_PAD = 3;
constexpr int32_t SEG_RADIUS = 9;
constexpr int32_t SEG_ITEM_RADIUS = 7;
constexpr int32_t SEG_ITEM_PAD_HOR = 16;
constexpr int32_t SEG_ITEM_PAD_VER = 7;
constexpr int32_t DROPDOWN_WIDTH = 200;
constexpr int32_t DROPDOWN_PAD = 10;
constexpr int32_t SPINNER_SIZE = 32;
constexpr int32_t SPINNER_WIDTH = 4;
constexpr uint32_t SPINNER_PERIOD_MS = 1000U;
constexpr uint32_t SPINNER_ARC_DEG = 270U;
constexpr int32_t TOAST_PAD_HOR = 18;
constexpr int32_t TOAST_PAD_VER = 10;
constexpr int32_t TOAST_BOTTOM = 24;
constexpr lv_opa_t TOAST_OPA = 235;
constexpr uint32_t TOAST_DEFAULT_MS = 2000U;
constexpr uint32_t TOAST_FADE_MS = 300U;
constexpr uint32_t ANIM_DEFAULT_MS = 300U;
constexpr int32_t PERCENT = 100;
constexpr size_t MAX_ALERT_BUTTONS = fw::alert::MAX_ACTIONS;

lv_opa_t percent_to_opa(lua_Integer pct)
{
    const lua_Integer clamped = (pct < 0) ? 0 : ((pct > PERCENT) ? PERCENT : pct);
    return static_cast<lv_opa_t>((clamped * LV_OPA_COVER) / PERCENT);
}

uint32_t color_arg(lua_State* p_lua, int arg)
{
    return static_cast<uint32_t>(luaL_checkinteger(p_lua, arg));
}

/** @brief Đọc trường tùy chọn của bảng ở vị trí table_arg; trả về false nếu không có (stack giữ nguyên). */
bool field(lua_State* p_lua, int table_arg, const char* p_key)
{
    if (lua_getfield(p_lua, table_arg, p_key) == LUA_TNIL) {
        lua_pop(p_lua, 1);
        return false;
    }
    return true; // Giá trị ở đỉnh stack: người gọi pop.
}

// ============================================================================
// Segmented control: container + các mục con (không đăng ký handle), chọn một
// ============================================================================
int32_t segment_selected(lv_obj_t* p_seg)
{
    const uint32_t count = lv_obj_get_child_count(p_seg);
    for (uint32_t index = 0U; index < count; ++index) {
        if (lv_obj_has_state(lv_obj_get_child(p_seg, static_cast<int32_t>(index)), LV_STATE_CHECKED)) {
            return static_cast<int32_t>(index) + 1;
        }
    }
    return 0;
}

void segment_select(lv_obj_t* p_seg, int32_t one_based)
{
    const uint32_t count = lv_obj_get_child_count(p_seg);
    for (uint32_t index = 0U; index < count; ++index) {
        lv_obj_t* const p_item = lv_obj_get_child(p_seg, static_cast<int32_t>(index));
        if ((static_cast<int32_t>(index) + 1) == one_based) {
            lv_obj_add_state(p_item, LV_STATE_CHECKED);
        } else {
            lv_obj_remove_state(p_item, LV_STATE_CHECKED);
        }
    }
}

void on_segment_clicked(lv_event_t* p_event)
{
    lv_obj_t* const p_item = lv_event_get_current_target_obj(p_event);
    lv_obj_t* const p_seg = lv_obj_get_parent(p_item);
    const int32_t index = lv_obj_get_index(p_item) + 1;
    if (segment_selected(p_seg) == index) {
        return;
    }
    segment_select(p_seg, index);
    lv_obj_send_event(p_seg, LV_EVENT_VALUE_CHANGED, nullptr);
}

// ============================================================================
// Hiệu ứng: hàm ghi thuộc tính cho lv_anim
// ============================================================================
void anim_x(void* p_var, int32_t value)
{
    lv_obj_set_x(static_cast<lv_obj_t*>(p_var), value);
}
void anim_y(void* p_var, int32_t value)
{
    lv_obj_set_y(static_cast<lv_obj_t*>(p_var), value);
}
void anim_w(void* p_var, int32_t value)
{
    lv_obj_set_width(static_cast<lv_obj_t*>(p_var), value);
}
void anim_h(void* p_var, int32_t value)
{
    lv_obj_set_height(static_cast<lv_obj_t*>(p_var), value);
}
void anim_opa(void* p_var, int32_t value)
{
    lv_obj_set_style_opa(static_cast<lv_obj_t*>(p_var), static_cast<lv_opa_t>(value), 0);
}

lv_anim_path_cb_t ease_path(const char* p_name)
{
    static const struct {
        const char* p_name;
        lv_anim_path_cb_t path;
    } PATHS[] = {{"linear", lv_anim_path_linear},     {"in", lv_anim_path_ease_in},
                 {"out", lv_anim_path_ease_out},       {"in_out", lv_anim_path_ease_in_out},
                 {"overshoot", lv_anim_path_overshoot}, {"bounce", lv_anim_path_bounce}};
    for (const auto& entry : PATHS) {
        if (std::strcmp(entry.p_name, p_name) == 0) {
            return entry.path;
        }
    }
    return nullptr;
}

// ============================================================================
// ui.style: bảng tùy chọn flex
// ============================================================================
lv_flex_align_t flex_align_arg(lua_State* p_lua, int arg)
{
    static const char* const NAMES[] = {"start", "center", "end", "between", "around", "evenly", nullptr};
    static const lv_flex_align_t VALUES[] = {LV_FLEX_ALIGN_START,         LV_FLEX_ALIGN_CENTER,
                                             LV_FLEX_ALIGN_END,           LV_FLEX_ALIGN_SPACE_BETWEEN,
                                             LV_FLEX_ALIGN_SPACE_AROUND,  LV_FLEX_ALIGN_SPACE_EVENLY};
    return VALUES[luaL_checkoption(p_lua, arg, nullptr, NAMES)];
}

} // namespace

// ============================================================================
// Bindings v6
// ============================================================================
struct UiV6 {
    // ---- Sự kiện ----
    static int on(lua_State* p_lua)
    {
        ScriptHost* const p_host = ScriptHost::from_lua(p_lua);
        lv_obj_t* const p_obj = ScriptHost::object_arg(p_lua, 1);
        const auto handle = static_cast<int16_t>(luaL_checkinteger(p_lua, 1));
        const auto kind = static_cast<uint8_t>(luaL_checkoption(p_lua, 2, nullptr, EVENT_NAMES));
        luaL_checktype(p_lua, 3, LUA_TFUNCTION);

        ScriptHost::EventBinding* p_slot = nullptr;
        for (ScriptHost::EventBinding& binding : p_host->m_bindings) {
            if ((binding.handle == handle) && (binding.kind == kind)) {
                p_slot = &binding;
                break;
            }
        }
        if (p_slot == nullptr) {
            for (ScriptHost::EventBinding& binding : p_host->m_bindings) {
                if (binding.handle == 0) {
                    p_slot = &binding;
                    break;
                }
            }
            if (p_slot == nullptr) {
                return luaL_error(p_lua, "too many event handlers (max %d)",
                                  static_cast<int>(ScriptHost::MAX_BINDINGS));
            }
            *p_slot = ScriptHost::EventBinding{handle, kind, LUA_NOREF};
            // Đăng ký với LVGL một lần cho mỗi (widget, loại); ui.off chỉ bỏ ref, giữ ô để on lại không nhân đôi.
            lv_obj_add_event_cb(p_obj, &ScriptHost::on_ui_event, EVENT_CODES[kind], p_host);
            prepare_for(p_obj, kind);
        } else if (p_slot->ref != LUA_NOREF) {
            luaL_unref(p_lua, LUA_REGISTRYINDEX, p_slot->ref);
        }
        lua_pushvalue(p_lua, 3);
        p_slot->ref = luaL_ref(p_lua, LUA_REGISTRYINDEX);
        return 0;
    }

    /** @brief Bật khả năng nhận chạm cần cho loại sự kiện (box/column/label mặc định không nhận chạm). */
    static void prepare_for(lv_obj_t* p_obj, uint8_t kind)
    {
        const bool needs_touch = (kind == EV_CLICK) || (kind == EV_LONG_PRESS) || (kind == EV_PRESS) ||
                                 (kind == EV_RELEASE) || (kind == EV_SWIPE);
        if (needs_touch) {
            lv_obj_set_clickable(p_obj, true);
        }
        if (kind == EV_SWIPE) {
            lv_obj_set_scrollable(p_obj, false);   // Cuộn "nuốt" cử chỉ vuốt.
            lv_obj_set_scroll_chain(p_obj, false); // Kéo trong vùng vuốt không cuộn trang cha.
            lv_obj_set_gesture_bubble(p_obj, false);
        }
    }

    static int off(lua_State* p_lua)
    {
        ScriptHost* const p_host = ScriptHost::from_lua(p_lua);
        (void)ScriptHost::object_arg(p_lua, 1);
        const auto handle = static_cast<int16_t>(luaL_checkinteger(p_lua, 1));
        const int kind = lua_isnoneornil(p_lua, 2) ? -1 : luaL_checkoption(p_lua, 2, nullptr, EVENT_NAMES);
        for (ScriptHost::EventBinding& binding : p_host->m_bindings) {
            if ((binding.handle == handle) && ((kind < 0) || (binding.kind == kind)) && (binding.ref != LUA_NOREF)) {
                luaL_unref(p_lua, LUA_REGISTRYINDEX, binding.ref);
                binding.ref = LUA_NOREF;
            }
        }
        return 0;
    }

    // ---- Nút ----
    static void style_button(lua_State* p_lua, lv_obj_t* p_btn, const char* p_variant, const char* p_size,
                             uint32_t accent, bool is_pill)
    {
        int32_t height = BTN_HEIGHT_MD;
        int32_t pad = BTN_PAD_MD;
        const char* p_font = "small";
        if (std::strcmp(p_size, "sm") == 0) {
            height = BTN_HEIGHT_SM;
            pad = BTN_PAD_SM;
        } else if (std::strcmp(p_size, "lg") == 0) {
            height = BTN_HEIGHT_LG;
            pad = BTN_PAD_LG;
            p_font = "medium";
        } else if (std::strcmp(p_size, "md") != 0) {
            (void)luaL_error(p_lua, "size must be sm, md or lg");
        }

        lv_obj_remove_style_all(p_btn);
        lv_obj_set_size(p_btn, LV_SIZE_CONTENT, fw::theme::px(height));
        lv_obj_set_style_min_width(p_btn, fw::theme::px(height), 0);
        lv_obj_set_style_pad_hor(p_btn, fw::theme::px(pad), 0);
        lv_obj_set_style_radius(p_btn, is_pill ? LV_RADIUS_CIRCLE : fw::theme::px(BTN_RADIUS), 0);
        lv_obj_set_style_text_font(p_btn, ScriptHost::font_named(p_lua, p_font), 0);
        lv_obj_set_style_opa(p_btn, PRESSED_OPA, LV_STATE_PRESSED);
        lv_obj_set_style_opa(p_btn, DISABLED_OPA, LV_STATE_DISABLED);

        uint32_t text = fw::palette::LABEL;
        if ((std::strcmp(p_variant, "filled") == 0) || (std::strcmp(p_variant, "destructive") == 0)) {
            const uint32_t bg = (std::strcmp(p_variant, "destructive") == 0) ? fw::palette::DESTRUCTIVE : accent;
            lv_obj_set_style_bg_color(p_btn, lv_color_hex(bg), 0);
            lv_obj_set_style_bg_opa(p_btn, LV_OPA_COVER, 0);
        } else if (std::strcmp(p_variant, "tinted") == 0) {
            lv_obj_set_style_bg_color(p_btn, lv_color_hex(accent), 0);
            lv_obj_set_style_bg_opa(p_btn, BTN_TINT_OPA, 0);
            text = accent;
        } else if (std::strcmp(p_variant, "outline") == 0) {
            lv_obj_set_style_border_width(p_btn, fw::theme::px(BTN_BORDER), 0);
            lv_obj_set_style_border_color(p_btn, lv_color_hex(accent), 0);
            text = accent;
        } else if (std::strcmp(p_variant, "plain") == 0) {
            text = accent;
        } else if (std::strcmp(p_variant, "gray") == 0) {
            lv_obj_set_style_bg_color(p_btn, lv_color_hex(fw::palette::FILL), 0);
            lv_obj_set_style_bg_opa(p_btn, LV_OPA_COVER, 0);
        } else {
            (void)luaL_error(p_lua, "variant must be filled, tinted, gray, outline, plain or destructive");
        }
        lv_obj_set_style_text_color(p_btn, lv_color_hex(text), 0);
    }

    static int button(lua_State* p_lua)
    {
        lv_obj_t* const p_parent = ScriptHost::object_arg(p_lua, 1);
        luaL_checktype(p_lua, 2, LUA_TTABLE);
        const char* p_text = "";
        const char* p_icon = nullptr;
        const char* p_variant = "filled";
        const char* p_size = "md";
        uint32_t accent = fw::palette::ACCENT;
        bool is_pill = false;
        if (field(p_lua, 2, "text")) {
            p_text = luaL_checkstring(p_lua, -1);
            lua_pop(p_lua, 1); // Chuỗi vẫn sống: bảng tham số giữ nó.
        }
        if (field(p_lua, 2, "icon")) {
            p_icon = luaL_checkstring(p_lua, -1);
            lua_pop(p_lua, 1);
        }
        if (field(p_lua, 2, "variant")) {
            p_variant = luaL_checkstring(p_lua, -1);
            lua_pop(p_lua, 1);
        }
        if (field(p_lua, 2, "size")) {
            p_size = luaL_checkstring(p_lua, -1);
            lua_pop(p_lua, 1);
        }
        if (field(p_lua, 2, "color")) {
            accent = static_cast<uint32_t>(luaL_checkinteger(p_lua, -1));
            lua_pop(p_lua, 1);
        }
        if (field(p_lua, 2, "shape")) {
            is_pill = std::strcmp(luaL_checkstring(p_lua, -1), "pill") == 0;
            lua_pop(p_lua, 1);
        }

        lv_obj_t* const p_btn = lv_button_create(p_parent);
        style_button(p_lua, p_btn, p_variant, p_size, accent, is_pill);
        lv_obj_t* const p_label = lv_label_create(p_btn);
        if ((p_icon != nullptr) && (p_text[0] != '\0')) {
            lv_label_set_text_fmt(p_label, "%s  %s", p_icon, p_text);
        } else {
            lv_label_set_text(p_label, (p_icon != nullptr) ? p_icon : p_text);
        }
        lv_obj_center(p_label);
        return ScriptHost::push_object(p_lua, p_btn, 3, LV_EVENT_CLICKED);
    }

    static int set_enabled(lua_State* p_lua)
    {
        lv_obj_t* const p_obj = ScriptHost::object_arg(p_lua, 1);
        if (lua_toboolean(p_lua, 2) != 0) {
            lv_obj_remove_state(p_obj, LV_STATE_DISABLED);
        } else {
            lv_obj_add_state(p_obj, LV_STATE_DISABLED);
        }
        return 0;
    }

    static int set_visible(lua_State* p_lua)
    {
        lv_obj_set_hidden(ScriptHost::object_arg(p_lua, 1), lua_toboolean(p_lua, 2) == 0);
        return 0;
    }

    // ---- Widget nhập liệu ----
    static int toggle(lua_State* p_lua)
    {
        lv_obj_t* const p_sw = lv_switch_create(ScriptHost::object_arg(p_lua, 1));
        lv_obj_set_size(p_sw, fw::theme::px(SWITCH_W), fw::theme::px(SWITCH_H));
        lv_obj_set_style_bg_color(p_sw, lv_color_hex(fw::palette::FILL), LV_PART_MAIN);
        lv_obj_set_style_bg_color(p_sw, lv_color_hex(fw::palette::FILL), LV_PART_INDICATOR);
        lv_obj_set_style_bg_color(p_sw, lv_color_hex(fw::palette::SUCCESS),
                                  static_cast<lv_style_selector_t>(LV_PART_INDICATOR) | LV_STATE_CHECKED);
        lv_obj_set_style_bg_color(p_sw, lv_color_white(), LV_PART_KNOB);
        lv_obj_set_style_opa(p_sw, DISABLED_OPA, LV_STATE_DISABLED);
        if (lua_toboolean(p_lua, 2) != 0) {
            lv_obj_add_state(p_sw, LV_STATE_CHECKED);
        }
        return ScriptHost::push_object(p_lua, p_sw, 0, LV_EVENT_ALL);
    }

    static int checkbox(lua_State* p_lua)
    {
        lv_obj_t* const p_cb = lv_checkbox_create(ScriptHost::object_arg(p_lua, 1));
        lv_checkbox_set_text(p_cb, luaL_optstring(p_lua, 2, ""));
        lv_obj_set_style_text_color(p_cb, lv_color_hex(fw::palette::LABEL), LV_PART_MAIN);
        lv_obj_set_style_pad_column(p_cb, fw::theme::px(CHECK_GAP), LV_PART_MAIN);
        lv_obj_set_style_radius(p_cb, fw::theme::px(CHECK_RADIUS), LV_PART_INDICATOR);
        lv_obj_set_style_border_color(p_cb, lv_color_hex(fw::palette::FILL_HIGH), LV_PART_INDICATOR);
        lv_obj_set_style_bg_opa(p_cb, LV_OPA_TRANSP, LV_PART_INDICATOR);
        constexpr auto CHECKED = static_cast<lv_style_selector_t>(LV_PART_INDICATOR) | LV_STATE_CHECKED;
        lv_obj_set_style_bg_color(p_cb, lv_color_hex(fw::palette::ACCENT), CHECKED);
        lv_obj_set_style_bg_opa(p_cb, LV_OPA_COVER, CHECKED);
        lv_obj_set_style_border_color(p_cb, lv_color_hex(fw::palette::ACCENT), CHECKED);
        lv_obj_set_style_opa(p_cb, DISABLED_OPA, LV_STATE_DISABLED);
        if (lua_toboolean(p_lua, 3) != 0) {
            lv_obj_add_state(p_cb, LV_STATE_CHECKED);
        }
        return ScriptHost::push_object(p_lua, p_cb, 0, LV_EVENT_ALL);
    }

    static int segmented(lua_State* p_lua)
    {
        lv_obj_t* const p_parent = ScriptHost::object_arg(p_lua, 1);
        luaL_checktype(p_lua, 2, LUA_TTABLE);
        const auto count = static_cast<lua_Integer>(luaL_len(p_lua, 2));
        luaL_argcheck(p_lua, count >= 1, 2, "need at least one option");
        const lua_Integer selected = luaL_optinteger(p_lua, 3, 1);

        lv_obj_t* const p_seg = lv_obj_create(p_parent);
        lv_obj_remove_style_all(p_seg);
        lv_obj_set_size(p_seg, LV_SIZE_CONTENT, LV_SIZE_CONTENT);
        lv_obj_set_flex_flow(p_seg, LV_FLEX_FLOW_ROW);
        lv_obj_set_style_bg_color(p_seg, lv_color_hex(fw::palette::SURFACE_HIGH), 0);
        lv_obj_set_style_bg_opa(p_seg, LV_OPA_COVER, 0);
        lv_obj_set_style_radius(p_seg, fw::theme::px(SEG_RADIUS), 0);
        lv_obj_set_style_pad_all(p_seg, fw::theme::px(SEG_PAD), 0);
        lv_obj_set_style_pad_column(p_seg, fw::theme::px(SEG_PAD), 0);
        lv_obj_set_scrollable(p_seg, false);
        for (lua_Integer index = 1; index <= count; ++index) {
            (void)lua_geti(p_lua, 2, index);
            lv_obj_t* const p_item = lv_obj_create(p_seg);
            lv_obj_remove_style_all(p_item);
            lv_obj_set_size(p_item, LV_SIZE_CONTENT, LV_SIZE_CONTENT);
            lv_obj_set_flex_grow(p_item, 1);
            lv_obj_set_style_pad_hor(p_item, fw::theme::px(SEG_ITEM_PAD_HOR), 0);
            lv_obj_set_style_pad_ver(p_item, fw::theme::px(SEG_ITEM_PAD_VER), 0);
            lv_obj_set_style_radius(p_item, fw::theme::px(SEG_ITEM_RADIUS), 0);
            lv_obj_set_style_bg_color(p_item, lv_color_hex(fw::palette::FILL_HIGH), LV_STATE_CHECKED);
            lv_obj_set_style_bg_opa(p_item, LV_OPA_COVER, LV_STATE_CHECKED);
            lv_obj_set_style_opa(p_item, PRESSED_OPA, LV_STATE_PRESSED);
            lv_obj_set_scrollable(p_item, false);
            lv_obj_add_event_cb(p_item, &on_segment_clicked, LV_EVENT_CLICKED, nullptr);
            lv_obj_t* const p_label = lv_label_create(p_item);
            lv_label_set_text(p_label, luaL_checkstring(p_lua, -1));
            lv_obj_set_style_text_color(p_label, lv_color_hex(fw::palette::LABEL), 0);
            lv_obj_center(p_label);
            lua_pop(p_lua, 1);
        }
        segment_select(p_seg, static_cast<int32_t>(selected));
        const int pushed = ScriptHost::push_object(p_lua, p_seg, 0, LV_EVENT_ALL);
        set_kind(p_lua, KIND_SEGMENTED);
        return pushed;
    }

    static int dropdown(lua_State* p_lua)
    {
        lv_obj_t* const p_parent = ScriptHost::object_arg(p_lua, 1);
        luaL_checktype(p_lua, 2, LUA_TTABLE);
        const auto count = static_cast<lua_Integer>(luaL_len(p_lua, 2));
        luaL_argcheck(p_lua, count >= 1, 2, "need at least one option");
        luaL_Buffer buf;
        luaL_buffinit(p_lua, &buf);
        for (lua_Integer index = 1; index <= count; ++index) {
            if (index > 1) {
                luaL_addchar(&buf, '\n');
            }
            (void)lua_geti(p_lua, 2, index);
            luaL_argcheck(p_lua, lua_type(p_lua, -1) == LUA_TSTRING, 2, "options must be strings");
            luaL_addvalue(&buf);
        }
        luaL_pushresult(&buf);
        lv_obj_t* const p_dd = lv_dropdown_create(p_parent);
        lv_dropdown_set_options(p_dd, lua_tostring(p_lua, -1)); // LVGL tự chép chuỗi.
        lua_pop(p_lua, 1);
        lv_dropdown_set_selected(p_dd, static_cast<uint32_t>(luaL_optinteger(p_lua, 3, 1) - 1));
        lv_obj_set_width(p_dd, fw::theme::px(DROPDOWN_WIDTH));
        lv_obj_set_style_bg_color(p_dd, lv_color_hex(fw::palette::SURFACE), 0);
        lv_obj_set_style_border_color(p_dd, lv_color_hex(fw::palette::FILL), 0);
        lv_obj_set_style_text_color(p_dd, lv_color_hex(fw::palette::LABEL), 0);
        lv_obj_set_style_radius(p_dd, fw::theme::px(BTN_RADIUS), 0);
        lv_obj_set_style_pad_all(p_dd, fw::theme::px(DROPDOWN_PAD), 0);
        lv_obj_t* const p_list = lv_dropdown_get_list(p_dd);
        if (p_list != nullptr) {
            lv_obj_set_style_bg_color(p_list, lv_color_hex(fw::palette::SURFACE_HIGH), 0);
            lv_obj_set_style_border_width(p_list, 0, 0);
            lv_obj_set_style_text_color(p_list, lv_color_hex(fw::palette::LABEL), 0);
            lv_obj_set_style_bg_color(p_list, lv_color_hex(fw::palette::ACCENT),
                                      static_cast<lv_style_selector_t>(LV_PART_SELECTED) | LV_STATE_CHECKED);
        }
        const int pushed = ScriptHost::push_object(p_lua, p_dd, 0, LV_EVENT_ALL);
        set_kind(p_lua, KIND_DROPDOWN);
        return pushed;
    }

    static int spinner(lua_State* p_lua)
    {
        lv_obj_t* const p_spin = lv_spinner_create(ScriptHost::object_arg(p_lua, 1));
        const int32_t size = ScriptHost::dp(p_lua, luaL_optinteger(p_lua, 2, SPINNER_SIZE));
        lv_obj_set_size(p_spin, size, size);
        lv_spinner_set_anim_params(p_spin, SPINNER_PERIOD_MS, SPINNER_ARC_DEG);
        lv_obj_set_style_arc_width(p_spin, fw::theme::px(SPINNER_WIDTH), LV_PART_MAIN);
        lv_obj_set_style_arc_width(p_spin, fw::theme::px(SPINNER_WIDTH), LV_PART_INDICATOR);
        lv_obj_set_style_arc_color(p_spin, lv_color_hex(fw::palette::FILL), LV_PART_MAIN);
        lv_obj_set_style_arc_color(p_spin, lv_color_hex(fw::palette::ACCENT), LV_PART_INDICATOR);
        lv_obj_set_clickable(p_spin, false);
        return ScriptHost::push_object(p_lua, p_spin, 0, LV_EVENT_ALL);
    }

    static int textfield(lua_State* p_lua)
    {
        ScriptHost* const p_host = ScriptHost::from_lua(p_lua);
        lv_obj_t* const p_parent = ScriptHost::object_arg(p_lua, 1);
        const bool has_opts = lua_istable(p_lua, 2);
        lv_obj_t* const p_ta = lv_textarea_create(p_parent);
        lv_textarea_set_one_line(p_ta, true);
        fw::keyboard::style_field(p_ta);
        lv_obj_set_width(p_ta, LV_PCT(100));
        if (has_opts) {
            if (field(p_lua, 2, "max")) {
                lv_textarea_set_max_length(p_ta, static_cast<uint32_t>(luaL_checkinteger(p_lua, -1)));
                lua_pop(p_lua, 1);
            }
            if (field(p_lua, 2, "placeholder")) {
                lv_textarea_set_placeholder_text(p_ta, luaL_checkstring(p_lua, -1));
                lua_pop(p_lua, 1);
            }
            if (field(p_lua, 2, "password")) {
                lv_textarea_set_password_mode(p_ta, lua_toboolean(p_lua, -1) != 0);
                lua_pop(p_lua, 1);
            }
            if (field(p_lua, 2, "text")) {
                lv_textarea_set_text(p_ta, luaL_checkstring(p_lua, -1));
                lua_pop(p_lua, 1);
            }
            if (field(p_lua, 2, "width")) {
                lv_obj_set_width(p_ta, ScriptHost::length_arg(p_lua, lua_gettop(p_lua)));
                lua_pop(p_lua, 1);
            }
        }
        lv_obj_add_event_cb(p_ta, &ScriptHost::on_field_event, LV_EVENT_FOCUSED, p_host);
        const int pushed = ScriptHost::push_object(p_lua, p_ta, 0, LV_EVENT_ALL);
        set_kind(p_lua, KIND_TEXTFIELD);
        return pushed;
    }

    /** @brief Ghi loại widget cho handle vừa push ở đỉnh stack. */
    static void set_kind(lua_State* p_lua, uint8_t kind)
    {
        const lua_Integer handle = lua_tointeger(p_lua, -1);
        ScriptHost::from_lua(p_lua)->m_widget_kind[handle - 1] = kind;
    }

    /** @brief Đẩy giá trị hiện tại của widget (nil nếu không có). */
    static void push_value(lua_State* p_lua, ScriptHost& host, lv_obj_t* p_obj)
    {
        const auto handle = static_cast<int32_t>(reinterpret_cast<intptr_t>(lv_obj_get_user_data(p_obj)));
        const uint8_t kind = ((handle >= 1) && (handle <= static_cast<int32_t>(ScriptHost::MAX_OBJECTS)))
                                 ? host.m_widget_kind[handle - 1]
                                 : static_cast<uint8_t>(KIND_NONE);
        if (kind == KIND_SEGMENTED) {
            lua_pushinteger(p_lua, segment_selected(p_obj));
        } else if (kind == KIND_TEXTFIELD) {
            lua_pushstring(p_lua, lv_textarea_get_text(p_obj));
        } else if (kind == KIND_DROPDOWN) {
            lua_pushinteger(p_lua, static_cast<lua_Integer>(lv_dropdown_get_selected(p_obj)) + 1);
        } else if (lv_obj_check_type(p_obj, &lv_switch_class) || lv_obj_check_type(p_obj, &lv_checkbox_class)) {
            lua_pushboolean(p_lua, lv_obj_has_state(p_obj, LV_STATE_CHECKED) ? 1 : 0);
        } else if (lv_obj_check_type(p_obj, &lv_slider_class)) {
            lua_pushinteger(p_lua, lv_slider_get_value(p_obj));
        } else if (lv_obj_check_type(p_obj, &lv_bar_class)) {
            lua_pushinteger(p_lua, lv_bar_get_value(p_obj));
        } else if (lv_obj_check_type(p_obj, &lv_arc_class)) {
            lua_pushinteger(p_lua, lv_arc_get_value(p_obj));
        } else {
            lua_pushnil(p_lua);
        }
    }

    static int get_value(lua_State* p_lua)
    {
        push_value(p_lua, *ScriptHost::from_lua(p_lua), ScriptHost::object_arg(p_lua, 1));
        return 1;
    }

    static int get_text(lua_State* p_lua)
    {
        lv_obj_t* p_obj = ScriptHost::object_arg(p_lua, 1);
        if (lv_obj_check_type(p_obj, &lv_textarea_class)) {
            lua_pushstring(p_lua, lv_textarea_get_text(p_obj));
            return 1;
        }
        if (lv_obj_check_type(p_obj, &lv_checkbox_class)) {
            lua_pushstring(p_lua, lv_checkbox_get_text(p_obj));
            return 1;
        }
        if (!lv_obj_check_type(p_obj, &lv_label_class)) {
            p_obj = lv_obj_get_child(p_obj, 0); // Nút / mục list: chữ của label con.
        }
        lua_pushstring(p_lua, ((p_obj != nullptr) && lv_obj_check_type(p_obj, &lv_label_class))
                                  ? lv_label_get_text(p_obj)
                                  : "");
        return 1;
    }

    // ---- Giao diện hệ thống ----
    static int alert(lua_State* p_lua)
    {
        ScriptHost* const p_host = ScriptHost::from_lua(p_lua);
        luaL_checktype(p_lua, 1, LUA_TTABLE);
        lua_settop(p_lua, 1);
        (void)p_host->dismiss_system_ui(); // Một hộp thoại / bàn phím tại một thời điểm.

        const char* p_title = "";
        const char* p_message = nullptr;
        if (field(p_lua, 1, "title")) {
            p_title = luaL_checkstring(p_lua, -1);
            lua_pop(p_lua, 1);
        }
        if (field(p_lua, 1, "message")) {
            p_message = luaL_checkstring(p_lua, -1);
            lua_pop(p_lua, 1);
        }
        bool is_destructive = false;
        if (field(p_lua, 1, "destructive")) {
            is_destructive = lua_toboolean(p_lua, -1) != 0;
            lua_pop(p_lua, 1);
        }
        fw::alert::Action actions[MAX_ALERT_BUTTONS] = {};
        size_t count = 0U;
        if (field(p_lua, 1, "buttons")) {
            luaL_checktype(p_lua, -1, LUA_TTABLE);
            const auto len = static_cast<size_t>(luaL_len(p_lua, -1));
            count = (len < MAX_ALERT_BUTTONS) ? len : MAX_ALERT_BUTTONS;
            for (size_t index = 0U; index < count; ++index) {
                (void)lua_geti(p_lua, -1, static_cast<lua_Integer>(index + 1U));
                actions[index].p_text = luaL_checkstring(p_lua, -1); // Bảng buttons còn trên stack giữ chuỗi.
                lua_pop(p_lua, 1);
            }
        }
        if (count == 0U) {
            actions[0].p_text = "OK";
            count = 1U;
        }
        for (size_t index = 0U; index < count; ++index) {
            const bool is_last = (index + 1U) == count;
            actions[index].color = (is_last && is_destructive) ? fw::palette::DESTRUCTIVE : fw::palette::ACCENT;
            actions[index].cb = &ScriptHost::on_alert_button;
        }
        p_host->m_p_alert = fw::alert::show(p_title, p_message, actions, count, p_host);
        if (field(p_lua, 1, "fn")) {
            luaL_checktype(p_lua, -1, LUA_TFUNCTION);
            p_host->m_alert_ref = luaL_ref(p_lua, LUA_REGISTRYINDEX);
        }
        return 0;
    }

    static int toast(lua_State* p_lua)
    {
        ScriptHost* const p_host = ScriptHost::from_lua(p_lua);
        const char* const p_text = luaL_checkstring(p_lua, 1);
        const auto duration = static_cast<uint32_t>(luaL_optinteger(p_lua, 2, TOAST_DEFAULT_MS));
        if (p_host->m_p_toast != nullptr) {
            lv_obj_delete(p_host->m_p_toast); // Sự kiện DELETE đặt m_p_toast = nullptr.
        }
        lv_obj_t* const p_toast = lv_label_create(lv_layer_top());
        lv_label_set_text(p_toast, p_text);
        lv_obj_set_style_bg_color(p_toast, lv_color_hex(fw::palette::SURFACE_HIGH), 0);
        lv_obj_set_style_bg_opa(p_toast, TOAST_OPA, 0);
        lv_obj_set_style_radius(p_toast, LV_RADIUS_CIRCLE, 0);
        lv_obj_set_style_pad_hor(p_toast, fw::theme::px(TOAST_PAD_HOR), 0);
        lv_obj_set_style_pad_ver(p_toast, fw::theme::px(TOAST_PAD_VER), 0);
        lv_obj_set_style_text_color(p_toast, lv_color_hex(fw::palette::LABEL), 0);
        lv_obj_set_style_text_font(p_toast, ScriptHost::font_named(p_lua, "small"), 0);
        lv_obj_align(p_toast, LV_ALIGN_BOTTOM_MID, 0, -fw::theme::px(TOAST_BOTTOM));
        lv_obj_add_event_cb(p_toast, &ScriptHost::on_toast_deleted, LV_EVENT_DELETE, p_host);
        lv_obj_fade_out(p_toast, TOAST_FADE_MS, duration);
        lv_obj_delete_delayed(p_toast, duration + TOAST_FADE_MS);
        p_host->m_p_toast = p_toast;
        return 0;
    }

    // ---- Kiểu dáng gom một lệnh ----
    static void apply_style(lua_State* p_lua, lv_obj_t* p_obj, const char* p_key, int value)
    {
        const auto color = [&]() { return lv_color_hex(color_arg(p_lua, value)); };
        const auto dp_value = [&]() { return ScriptHost::dp(p_lua, luaL_checkinteger(p_lua, value)); };
        if (std::strcmp(p_key, "bg") == 0) {
            lv_obj_set_style_bg_color(p_obj, color(), 0);
            lv_obj_set_style_bg_opa(p_obj, LV_OPA_COVER, 0);
        } else if (std::strcmp(p_key, "bg_opa") == 0) {
            lv_obj_set_style_bg_opa(p_obj, percent_to_opa(luaL_checkinteger(p_lua, value)), 0);
        } else if (std::strcmp(p_key, "radius") == 0) {
            const bool is_round = (lua_type(p_lua, value) == LUA_TSTRING) &&
                                  (std::strcmp(lua_tostring(p_lua, value), "round") == 0);
            lv_obj_set_style_radius(p_obj, is_round ? LV_RADIUS_CIRCLE : dp_value(), 0);
        } else if (std::strcmp(p_key, "pad") == 0) {
            lv_obj_set_style_pad_all(p_obj, dp_value(), 0);
        } else if (std::strcmp(p_key, "pad_x") == 0) {
            lv_obj_set_style_pad_hor(p_obj, dp_value(), 0);
        } else if (std::strcmp(p_key, "pad_y") == 0) {
            lv_obj_set_style_pad_ver(p_obj, dp_value(), 0);
        } else if (std::strcmp(p_key, "gap") == 0) {
            lv_obj_set_style_pad_gap(p_obj, dp_value(), 0);
        } else if (std::strcmp(p_key, "border") == 0) {
            lv_obj_set_style_border_width(p_obj, dp_value(), 0);
        } else if (std::strcmp(p_key, "border_color") == 0) {
            lv_obj_set_style_border_color(p_obj, color(), 0);
        } else if (std::strcmp(p_key, "shadow") == 0) {
            lv_obj_set_style_shadow_width(p_obj, dp_value(), 0);
            lv_obj_set_style_shadow_opa(p_obj, LV_OPA_50, 0);
        } else if (std::strcmp(p_key, "shadow_color") == 0) {
            lv_obj_set_style_shadow_color(p_obj, color(), 0);
        } else if (std::strcmp(p_key, "opa") == 0) {
            lv_obj_set_style_opa(p_obj, percent_to_opa(luaL_checkinteger(p_lua, value)), 0);
        } else if (std::strcmp(p_key, "color") == 0) {
            lv_obj_set_style_text_color(p_obj, color(), 0);
        } else if (std::strcmp(p_key, "font") == 0) {
            lv_obj_set_style_text_font(p_obj, ScriptHost::font_named(p_lua, luaL_checkstring(p_lua, value)), 0);
        } else if (std::strcmp(p_key, "text_align") == 0) {
            static const char* const NAMES[] = {"left", "center", "right", nullptr};
            static const lv_text_align_t ALIGNS[] = {LV_TEXT_ALIGN_LEFT, LV_TEXT_ALIGN_CENTER, LV_TEXT_ALIGN_RIGHT};
            lv_obj_set_style_text_align(p_obj, ALIGNS[luaL_checkoption(p_lua, value, nullptr, NAMES)], 0);
        } else if (std::strcmp(p_key, "width") == 0) {
            lv_obj_set_width(p_obj, ScriptHost::length_arg(p_lua, value));
        } else if (std::strcmp(p_key, "height") == 0) {
            lv_obj_set_height(p_obj, ScriptHost::length_arg(p_lua, value));
        } else if (std::strcmp(p_key, "grow") == 0) {
            lv_obj_set_flex_grow(p_obj, static_cast<uint8_t>(luaL_checkinteger(p_lua, value)));
        } else if (std::strcmp(p_key, "justify") == 0) {
            lv_obj_set_style_flex_main_place(p_obj, flex_align_arg(p_lua, value), 0);
        } else if (std::strcmp(p_key, "align") == 0) {
            const lv_flex_align_t cross = flex_align_arg(p_lua, value);
            lv_obj_set_style_flex_cross_place(p_obj, cross, 0);
            lv_obj_set_style_flex_track_place(p_obj, cross, 0);
        } else if (std::strcmp(p_key, "pressed_bg") == 0) {
            lv_obj_set_style_bg_color(p_obj, color(), LV_STATE_PRESSED);
            lv_obj_set_style_bg_opa(p_obj, LV_OPA_COVER, LV_STATE_PRESSED);
        } else if (std::strcmp(p_key, "pressed_opa") == 0) {
            lv_obj_set_style_opa(p_obj, percent_to_opa(luaL_checkinteger(p_lua, value)), LV_STATE_PRESSED);
        } else if (std::strcmp(p_key, "clip") == 0) {
            lv_obj_set_style_clip_corner(p_obj, lua_toboolean(p_lua, value) != 0, 0);
        } else if (std::strcmp(p_key, "scroll") == 0) {
            const bool is_on = lua_toboolean(p_lua, value) != 0;
            lv_obj_set_scrollable(p_obj, is_on);
            if (is_on) {
                // LVGL chỉ bắt đầu cuộn từ object NHẬN CHẠM dưới ngón tay rồi đi ngược lên cha; row/column mặc định
                // không nhận chạm -> kéo trên nền/chữ/khoảng trống sẽ không cuộn. Vùng cuộn phải tự bắt cú kéo.
                lv_obj_set_clickable(p_obj, true);
            }
        } else {
            (void)luaL_error(p_lua, "unknown style key '%s'", p_key);
        }
    }

    static int style(lua_State* p_lua)
    {
        lv_obj_t* const p_obj = ScriptHost::object_arg(p_lua, 1);
        luaL_checktype(p_lua, 2, LUA_TTABLE);
        lua_settop(p_lua, 2);
        lua_pushnil(p_lua);
        while (lua_next(p_lua, 2) != 0) {
            luaL_argcheck(p_lua, lua_type(p_lua, -2) == LUA_TSTRING, 2, "style keys must be strings");
            apply_style(p_lua, p_obj, lua_tostring(p_lua, -2), lua_gettop(p_lua));
            lua_pop(p_lua, 1);
        }
        return 0;
    }

    // ---- Hiệu ứng ----
    static int animate(lua_State* p_lua)
    {
        ScriptHost* const p_host = ScriptHost::from_lua(p_lua);
        lv_obj_t* const p_obj = ScriptHost::object_arg(p_lua, 1);
        const auto handle = static_cast<int32_t>(luaL_checkinteger(p_lua, 1));
        luaL_checktype(p_lua, 2, LUA_TTABLE);
        lua_settop(p_lua, 2);

        uint32_t duration = ANIM_DEFAULT_MS;
        uint32_t delay = 0U;
        lv_anim_path_cb_t path = lv_anim_path_ease_out;
        if (field(p_lua, 2, "ms")) {
            duration = static_cast<uint32_t>(luaL_checkinteger(p_lua, -1));
            lua_pop(p_lua, 1);
        }
        if (field(p_lua, 2, "delay")) {
            delay = static_cast<uint32_t>(luaL_checkinteger(p_lua, -1));
            lua_pop(p_lua, 1);
        }
        if (field(p_lua, 2, "ease")) {
            path = ease_path(luaL_checkstring(p_lua, -1));
            luaL_argcheck(p_lua, path != nullptr, 2, "ease must be linear, in, out, in_out, overshoot or bounce");
            lua_pop(p_lua, 1);
        }

        lv_obj_update_layout(p_obj); // Giá trị bắt đầu = vị trí / kích thước đang hiển thị.
        static const struct {
            const char* p_key;
            lv_anim_exec_xcb_t exec;
        } PROPS[] = {{"x", anim_x}, {"y", anim_y}, {"w", anim_w}, {"h", anim_h}, {"opa", anim_opa}};
        lv_anim_t anims[sizeof(PROPS) / sizeof(PROPS[0])];
        size_t count = 0U;
        for (const auto& prop : PROPS) {
            if (!field(p_lua, 2, prop.p_key)) {
                continue;
            }
            const lua_Integer target = luaL_checkinteger(p_lua, -1);
            lua_pop(p_lua, 1);
            int32_t start = 0;
            int32_t end = 0;
            if (prop.exec == anim_opa) {
                start = lv_obj_get_style_opa(p_obj, LV_PART_MAIN);
                end = percent_to_opa(target);
            } else {
                start = (prop.exec == anim_x)   ? lv_obj_get_x(p_obj)
                        : (prop.exec == anim_y) ? lv_obj_get_y(p_obj)
                        : (prop.exec == anim_w) ? lv_obj_get_width(p_obj)
                                                : lv_obj_get_height(p_obj);
                end = ScriptHost::dp(p_lua, target);
            }
            lv_anim_t& anim = anims[count++];
            lv_anim_init(&anim);
            lv_anim_set_var(&anim, p_obj);
            lv_anim_set_exec_cb(&anim, prop.exec);
            lv_anim_set_values(&anim, start, end);
            lv_anim_set_duration(&anim, duration);
            lv_anim_set_delay(&anim, delay);
            lv_anim_set_path_cb(&anim, path);
        }
        luaL_argcheck(p_lua, count > 0U, 2, "nothing to animate (use x, y, w, h or opa)");

        int& done_ref = p_host->m_anim_refs[handle - 1];
        if (done_ref != LUA_NOREF) {
            luaL_unref(p_lua, LUA_REGISTRYINDEX, done_ref);
            done_ref = LUA_NOREF;
        }
        if (field(p_lua, 2, "done")) {
            luaL_checktype(p_lua, -1, LUA_TFUNCTION);
            done_ref = luaL_ref(p_lua, LUA_REGISTRYINDEX);
            lv_anim_set_completed_cb(&anims[count - 1U], &ScriptHost::on_anim_done);
            lv_anim_set_user_data(&anims[count - 1U], p_host);
        }
        for (size_t index = 0U; index < count; ++index) {
            (void)lv_anim_start(&anims[index]);
        }
        return 0;
    }
};

// ============================================================================
// ScriptHost: phần v6
// ============================================================================
void ScriptHost::register_ui_v6(lua_State* p_lua)
{
    static const luaL_Reg V6[] = {{"on", UiV6::on},
                                  {"off", UiV6::off},
                                  {"set_enabled", UiV6::set_enabled},
                                  {"set_visible", UiV6::set_visible},
                                  {"switch", UiV6::toggle},
                                  {"checkbox", UiV6::checkbox},
                                  {"segmented", UiV6::segmented},
                                  {"dropdown", UiV6::dropdown},
                                  {"spinner", UiV6::spinner},
                                  {"textfield", UiV6::textfield},
                                  {"get_value", UiV6::get_value},
                                  {"get_text", UiV6::get_text},
                                  {"alert", UiV6::alert},
                                  {"toast", UiV6::toast},
                                  {"style", UiV6::style},
                                  {"animate", UiV6::animate},
                                  {nullptr, nullptr}};
    luaL_setfuncs(p_lua, V6, 0);
}

int ScriptHost::ui_button_v6(lua_State* p_lua)
{
    return UiV6::button(p_lua);
}

bool ScriptHost::set_value_v6(lua_State* p_lua, lv_obj_t* p_obj, int arg)
{
    ScriptHost* const p_host = from_lua(p_lua);
    const auto handle = static_cast<int32_t>(reinterpret_cast<intptr_t>(lv_obj_get_user_data(p_obj)));
    const uint8_t kind = p_host->m_widget_kind[handle - 1];
    if (kind == KIND_SEGMENTED) {
        segment_select(p_obj, static_cast<int32_t>(luaL_checkinteger(p_lua, arg)));
    } else if (kind == KIND_DROPDOWN) {
        lv_dropdown_set_selected(p_obj, static_cast<uint32_t>(luaL_checkinteger(p_lua, arg) - 1));
    } else if (kind == KIND_TEXTFIELD) {
        lv_textarea_set_text(p_obj, luaL_checkstring(p_lua, arg));
    } else if (lv_obj_check_type(p_obj, &lv_switch_class) || lv_obj_check_type(p_obj, &lv_checkbox_class)) {
        if (lua_toboolean(p_lua, arg) != 0) {
            lv_obj_add_state(p_obj, LV_STATE_CHECKED);
        } else {
            lv_obj_remove_state(p_obj, LV_STATE_CHECKED);
        }
    } else {
        return false;
    }
    return true;
}

void ScriptHost::release_object_extras(int32_t handle, lv_obj_t* p_obj)
{
    for (EventBinding& binding : m_bindings) {
        if (binding.handle == handle) {
            if ((m_p_lua != nullptr) && (binding.ref != LUA_NOREF)) {
                luaL_unref(m_p_lua, LUA_REGISTRYINDEX, binding.ref);
            }
            binding = EventBinding{0, 0U, LUA_NOREF};
        }
    }
    int& done_ref = m_anim_refs[handle - 1];
    if ((m_p_lua != nullptr) && (done_ref != LUA_NOREF)) {
        luaL_unref(m_p_lua, LUA_REGISTRYINDEX, done_ref);
    }
    done_ref = LUA_NOREF;
    // Bàn phím giữ con trỏ tới ô nhập: gỡ trước khi ô nhập bị giải phóng.
    if ((m_p_keyboard != nullptr) && (lv_keyboard_get_textarea(m_p_keyboard) == p_obj)) {
        lv_keyboard_set_textarea(m_p_keyboard, nullptr);
        hide_keyboard();
    }
    m_widget_kind[handle - 1] = KIND_NONE;
}

void ScriptHost::close_system_ui()
{
    if (m_p_keyboard != nullptr) {
        lv_obj_delete(m_p_keyboard);
        m_p_keyboard = nullptr;
    }
    if (m_p_root != nullptr) {
        lv_obj_set_style_translate_y(m_p_root, 0, 0);
    }
    if (m_p_alert != nullptr) {
        lv_msgbox_close_async(m_p_alert);
        m_p_alert = nullptr;
    }
    if ((m_p_lua != nullptr) && (m_alert_ref != LUA_NOREF)) {
        luaL_unref(m_p_lua, LUA_REGISTRYINDEX, m_alert_ref);
    }
    m_alert_ref = LUA_NOREF;
    if (m_p_toast != nullptr) {
        lv_obj_delete(m_p_toast);
    }
    for (size_t index = 0U; index < MAX_OBJECTS; ++index) {
        if ((m_objects[index] != nullptr) && (m_widget_kind[index] == KIND_DROPDOWN)) {
            lv_dropdown_close(m_objects[index]); // Danh sách mở nằm trên top layer.
        }
    }
}

bool ScriptHost::dismiss_system_ui()
{
    if ((m_p_keyboard != nullptr) && !lv_obj_is_hidden(m_p_keyboard)) {
        hide_keyboard();
        return true;
    }
    if (m_p_alert == nullptr) {
        return false;
    }
    lv_msgbox_close_async(m_p_alert);
    m_p_alert = nullptr;
    const int ref = m_alert_ref;
    m_alert_ref = LUA_NOREF;
    const int32_t dismissed = 0; // fn(0): đóng bằng Back, không chọn nút nào.
    invoke(ref, &dismissed);
    if ((m_p_lua != nullptr) && (ref != LUA_NOREF)) {
        luaL_unref(m_p_lua, LUA_REGISTRYINDEX, ref);
    }
    return true;
}

void ScriptHost::show_keyboard(lv_obj_t* p_field)
{
    if (m_p_keyboard == nullptr) {
        m_p_keyboard = lv_keyboard_create(lv_layer_top());
        lv_obj_set_size(m_p_keyboard, LV_PCT(100), fw::theme::px(fw::keyboard::HEIGHT));
        lv_obj_align(m_p_keyboard, LV_ALIGN_BOTTOM_MID, 0, 0);
        fw::keyboard::style(m_p_keyboard);
        lv_obj_add_event_cb(m_p_keyboard, &ScriptHost::on_keyboard_event, LV_EVENT_READY, this);
        lv_obj_add_event_cb(m_p_keyboard, &ScriptHost::on_keyboard_event, LV_EVENT_CANCEL, this);
    }
    lv_keyboard_set_textarea(m_p_keyboard, p_field);
    lv_obj_set_hidden(m_p_keyboard, false);

    // Ô nhập bị bàn phím che -> đẩy cả app lên vừa đủ (khôi phục khi ẩn bàn phím).
    if (m_p_root != nullptr) {
        lv_obj_set_style_translate_y(m_p_root, 0, 0);
        lv_obj_update_layout(m_p_root);
        lv_area_t area{};
        lv_obj_get_coords(p_field, &area);
        const int32_t keyboard_top = lv_display_get_vertical_resolution(nullptr) - fw::theme::px(fw::keyboard::HEIGHT);
        const int32_t margin = fw::theme::px(fw::keyboard::PAD);
        const int32_t overlap = (area.y2 + margin) - keyboard_top;
        if (overlap > 0) {
            lv_obj_set_style_translate_y(m_p_root, -overlap, 0);
        }
    }
}

void ScriptHost::hide_keyboard()
{
    if (m_p_keyboard == nullptr) {
        return;
    }
    lv_obj_t* const p_field = lv_keyboard_get_textarea(m_p_keyboard);
    if (p_field != nullptr) {
        lv_obj_remove_state(p_field, LV_STATE_FOCUSED);
    }
    lv_obj_set_hidden(m_p_keyboard, true);
    if (m_p_root != nullptr) {
        lv_obj_set_style_translate_y(m_p_root, 0, 0);
    }
}

void ScriptHost::push_event_arg(lv_obj_t* p_obj, uint8_t kind, int& nargs)
{
    nargs = 0;
    if (kind == EV_CHANGE) {
        UiV6::push_value(m_p_lua, *this, p_obj);
        nargs = 1;
    } else if ((kind == EV_SUBMIT) && lv_obj_check_type(p_obj, &lv_textarea_class)) {
        lua_pushstring(m_p_lua, lv_textarea_get_text(p_obj));
        nargs = 1;
    } else if (kind == EV_SWIPE) {
        lv_indev_t* const p_indev = lv_indev_active();
        const lv_dir_t dir = (p_indev != nullptr) ? lv_indev_get_gesture_dir(p_indev) : LV_DIR_NONE;
        const char* const p_dir = (dir == LV_DIR_LEFT)     ? "left"
                                  : (dir == LV_DIR_RIGHT)  ? "right"
                                  : (dir == LV_DIR_TOP)    ? "up"
                                  : (dir == LV_DIR_BOTTOM) ? "down"
                                                           : nullptr;
        if (p_dir == nullptr) {
            nargs = -1; // Không có hướng: bỏ sự kiện.
            return;
        }
        lv_indev_wait_release(p_indev); // Một lần vuốt = một sự kiện.
        lua_pushstring(m_p_lua, p_dir);
        nargs = 1;
    } else {
        // click, long_press, press, release, focus, blur: không có tham số.
    }
}

void ScriptHost::on_ui_event(lv_event_t* p_event)
{
    auto* const p_host = static_cast<ScriptHost*>(lv_event_get_user_data(p_event));
    lv_obj_t* const p_obj = lv_event_get_current_target_obj(p_event);
    const auto handle = static_cast<int32_t>(reinterpret_cast<intptr_t>(lv_obj_get_user_data(p_obj)));
    if ((handle < 1) || (handle > static_cast<int32_t>(MAX_OBJECTS)) || (p_host->m_objects[handle - 1] != p_obj) ||
        (p_host->m_p_lua == nullptr) || p_host->m_faulted || p_host->m_in_call) {
        return;
    }
    const lv_event_code_t code = lv_event_get_code(p_event);
    for (const EventBinding& binding : p_host->m_bindings) {
        if ((binding.handle != handle) || (EVENT_CODES[binding.kind] != code) || (binding.ref == LUA_NOREF)) {
            continue;
        }
        lua_State* const p_lua = p_host->m_p_lua;
        const int top = lua_gettop(p_lua);
        lua_rawgeti(p_lua, LUA_REGISTRYINDEX, binding.ref);
        int nargs = 0;
        p_host->push_event_arg(p_obj, binding.kind, nargs);
        if (nargs < 0) {
            lua_settop(p_lua, top);
            return;
        }
        (void)p_host->protected_call(nargs, 0);
        return; // Mỗi (widget, loại) chỉ có một binding.
    }
}

void ScriptHost::on_alert_button(lv_event_t* p_event)
{
    auto* const p_host = static_cast<ScriptHost*>(lv_event_get_user_data(p_event));
    const int32_t index = lv_obj_get_index(lv_event_get_current_target_obj(p_event)) + 1;
    if (p_host->m_p_alert != nullptr) {
        lv_msgbox_close_async(p_host->m_p_alert); // Async: đang ở trong callback của chính hộp thoại.
        p_host->m_p_alert = nullptr;
    }
    const int ref = p_host->m_alert_ref;
    p_host->m_alert_ref = LUA_NOREF;
    p_host->invoke(ref, &index);
    if ((p_host->m_p_lua != nullptr) && (ref != LUA_NOREF)) {
        luaL_unref(p_host->m_p_lua, LUA_REGISTRYINDEX, ref);
    }
}

void ScriptHost::on_keyboard_event(lv_event_t* p_event)
{
    // READY (OK) / CANCEL (ẩn bàn phím). LVGL gửi tiếp READY tới ô nhập sau hàm này -> sự kiện "submit".
    static_cast<ScriptHost*>(lv_event_get_user_data(p_event))->hide_keyboard();
}

void ScriptHost::on_field_event(lv_event_t* p_event)
{
    static_cast<ScriptHost*>(lv_event_get_user_data(p_event))->show_keyboard(lv_event_get_current_target_obj(p_event));
}

void ScriptHost::on_toast_deleted(lv_event_t* p_event)
{
    auto* const p_host = static_cast<ScriptHost*>(lv_event_get_user_data(p_event));
    if (p_host->m_p_toast == lv_event_get_current_target_obj(p_event)) {
        p_host->m_p_toast = nullptr;
    }
}

void ScriptHost::on_anim_done(lv_anim_t* p_anim)
{
    auto* const p_host = static_cast<ScriptHost*>(lv_anim_get_user_data(p_anim));
    auto* const p_obj = static_cast<lv_obj_t*>(p_anim->var);
    const auto handle = static_cast<int32_t>(reinterpret_cast<intptr_t>(lv_obj_get_user_data(p_obj)));
    if ((p_host == nullptr) || (handle < 1) || (handle > static_cast<int32_t>(MAX_OBJECTS)) ||
        (p_host->m_objects[handle - 1] != p_obj)) {
        return;
    }
    const int ref = p_host->m_anim_refs[handle - 1];
    p_host->m_anim_refs[handle - 1] = LUA_NOREF;
    p_host->invoke(ref, nullptr);
    if ((p_host->m_p_lua != nullptr) && (ref != LUA_NOREF)) {
        luaL_unref(p_host->m_p_lua, LUA_REGISTRYINDEX, ref);
    }
}

} // namespace runtime
