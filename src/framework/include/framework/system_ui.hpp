/**
 * @file    system_ui.hpp
 * @brief   Thành phần giao diện hệ thống dùng chung (app tích hợp + SDK Lua): hộp thoại kiểu iOS,
 *          bàn phím ảo và ô nhập. Một nguồn kiểu dáng -> mọi nơi trông giống nhau.
 *
 * Kích thước theo dp (theme::px). Chỉ gọi từ UI task.
 */
#ifndef FRAMEWORK_SYSTEM_UI_HPP
#define FRAMEWORK_SYSTEM_UI_HPP

#include <cstddef>
#include <cstdint>

#include "framework/theme.hpp"
#include "lvgl.h"

namespace fw {

// ============================================================================
// Bảng màu hệ thống (dark, kiểu iOS)
// ============================================================================
namespace palette {
constexpr uint32_t ACCENT = 0x0A84FFU;       /**< Màu nhấn (nút chính, liên kết, con trỏ). */
constexpr uint32_t DESTRUCTIVE = 0xFF453AU;  /**< Hành động nguy hiểm. */
constexpr uint32_t SUCCESS = 0x30D158U;      /**< Bật / thành công (switch). */
constexpr uint32_t SURFACE = 0x1C1C1EU;      /**< Nền thẻ / ô nhập. */
constexpr uint32_t SURFACE_HIGH = 0x2C2C2EU; /**< Nền nổi (alert, segmented). */
constexpr uint32_t FILL = 0x3A3A3CU;         /**< Viền / rãnh. */
constexpr uint32_t FILL_HIGH = 0x636366U;    /**< Mục đang chọn (segmented). */
constexpr uint32_t LABEL = 0xFFFFFFU;
constexpr uint32_t LABEL_SECONDARY = 0x8E8E93U;
} // namespace palette

// ============================================================================
// Hộp thoại xác nhận (alert)
// ============================================================================
namespace alert {

constexpr uint32_t COLOR_CARD = palette::SURFACE_HIGH;
constexpr uint32_t COLOR_PRESSED = 0x3A3A3CU;
constexpr uint32_t COLOR_SEPARATOR = 0x48484AU;
constexpr uint32_t COLOR_TITLE = palette::LABEL;
constexpr uint32_t COLOR_MESSAGE = 0xAEAEB2U;
constexpr uint32_t COLOR_CANCEL = palette::ACCENT;
constexpr uint32_t COLOR_DESTRUCTIVE = palette::DESTRUCTIVE;
constexpr uint32_t COLOR_DEFAULT = palette::ACCENT;
constexpr lv_opa_t BACKDROP_OPA = LV_OPA_50;
constexpr size_t MAX_ACTIONS = 2U; /**< Nút chia đôi chân thẻ. */

// ---- Kích thước (dp) ----
constexpr int32_t WIDTH = 300;
constexpr int32_t RADIUS = 16;
constexpr int32_t BODY_PAD_HOR = 20;
constexpr int32_t BODY_PAD_TOP = 22;
constexpr int32_t BODY_PAD_BOTTOM = 18;
constexpr int32_t BODY_GAP = 8;
constexpr int32_t BUTTON_HEIGHT = 48;
constexpr int32_t SEPARATOR_WIDTH = 1;

/** @brief Một nút của hộp thoại. */
struct Action {
    const char* p_text;
    uint32_t color;
    lv_event_cb_t cb; /**< Gọi với user_data truyền vào show(). */
};

/** @brief Nút phẳng chiếm một phần chân thẻ. */
inline void style_button(lv_obj_t* p_btn, const Action& action, bool is_bold, bool has_left_separator, void* p_user)
{
    lv_obj_remove_style_all(p_btn);
    lv_obj_set_height(p_btn, LV_PCT(100));
    lv_obj_set_flex_grow(p_btn, 1);
    lv_obj_set_style_bg_color(p_btn, lv_color_hex(COLOR_PRESSED), LV_STATE_PRESSED);
    lv_obj_set_style_bg_opa(p_btn, LV_OPA_COVER, LV_STATE_PRESSED);
    if (has_left_separator) {
        lv_obj_set_style_border_side(p_btn, LV_BORDER_SIDE_LEFT, 0);
        lv_obj_set_style_border_width(p_btn, theme::px(SEPARATOR_WIDTH), 0);
        lv_obj_set_style_border_color(p_btn, lv_color_hex(COLOR_SEPARATOR), 0);
    }
    lv_obj_t* const p_label = lv_obj_get_child(p_btn, 0);
    if (p_label != nullptr) {
        lv_obj_set_style_text_color(p_label, lv_color_hex(action.color), 0);
        lv_obj_set_style_text_font(p_label, is_bold ? theme::font_medium() : theme::font_small(), 0);
        lv_obj_center(p_label);
    }
    lv_obj_add_event_cb(p_btn, action.cb, LV_EVENT_CLICKED, p_user);
}

/**
 * @brief Hiện hộp thoại: nền mờ, thẻ bo góc giữa màn hình, tiêu đề + mô tả, 1-2 nút phẳng ở chân.
 *
 * @param[in] p_title   Tiêu đề (căn giữa).
 * @param[in] p_message Mô tả, nullptr = không có.
 * @param[in] p_actions Mảng nút (trái -> phải; nút cuối là hành động chính, chữ to hơn).
 * @param[in] count     1..MAX_ACTIONS (dư bị bỏ qua).
 * @param[in] p_user    user_data cho mọi callback.
 * @return Handle msgbox (đóng bằng lv_msgbox_close_async). Chỉ số nút bấm = lv_obj_get_index(nút).
 */
inline lv_obj_t* show(const char* p_title, const char* p_message, const Action* p_actions, size_t count, void* p_user)
{
    lv_obj_t* const p_box = lv_msgbox_create(nullptr); // nullptr -> modal trên top layer, có nền chặn chạm.
    lv_obj_t* const p_backdrop = lv_obj_get_parent(p_box);
    lv_obj_set_style_bg_color(p_backdrop, lv_color_black(), 0);
    lv_obj_set_style_bg_opa(p_backdrop, BACKDROP_OPA, 0);

    lv_obj_set_style_bg_color(p_box, lv_color_hex(COLOR_CARD), 0);
    lv_obj_set_style_bg_opa(p_box, LV_OPA_COVER, 0);
    lv_obj_set_style_radius(p_box, theme::px(RADIUS), 0);
    lv_obj_set_style_clip_corner(p_box, true, 0);
    lv_obj_set_style_border_width(p_box, 0, 0);
    lv_obj_set_style_shadow_width(p_box, 0, 0);
    lv_obj_set_style_pad_all(p_box, 0, 0);
    lv_obj_set_style_pad_row(p_box, 0, 0);
    lv_obj_set_width(p_box, theme::px(WIDTH));

    lv_obj_t* const p_content = lv_msgbox_get_content(p_box);
    lv_obj_set_style_pad_hor(p_content, theme::px(BODY_PAD_HOR), 0);
    lv_obj_set_style_pad_top(p_content, theme::px(BODY_PAD_TOP), 0);
    lv_obj_set_style_pad_bottom(p_content, theme::px(BODY_PAD_BOTTOM), 0);
    lv_obj_set_style_pad_row(p_content, theme::px(BODY_GAP), 0);
    lv_obj_set_flex_flow(p_content, LV_FLEX_FLOW_COLUMN);
    lv_obj_set_flex_align(p_content, LV_FLEX_ALIGN_START, LV_FLEX_ALIGN_CENTER, LV_FLEX_ALIGN_CENTER);

    lv_obj_t* const p_title_label = lv_msgbox_add_text(p_box, p_title);
    lv_obj_set_width(p_title_label, LV_PCT(100));
    lv_obj_set_style_text_align(p_title_label, LV_TEXT_ALIGN_CENTER, 0);
    lv_obj_set_style_text_font(p_title_label, theme::font_medium(), 0);
    lv_obj_set_style_text_color(p_title_label, lv_color_hex(COLOR_TITLE), 0);
    if (p_message != nullptr) {
        lv_obj_t* const p_msg = lv_msgbox_add_text(p_box, p_message);
        lv_obj_set_width(p_msg, LV_PCT(100));
        lv_obj_set_style_text_align(p_msg, LV_TEXT_ALIGN_CENTER, 0);
        lv_obj_set_style_text_font(p_msg, theme::font_small(), 0);
        lv_obj_set_style_text_color(p_msg, lv_color_hex(COLOR_MESSAGE), 0);
    }

    const size_t used = (count < MAX_ACTIONS) ? count : MAX_ACTIONS;
    for (size_t index = 0U; index < used; ++index) {
        lv_obj_t* const p_btn = lv_msgbox_add_footer_button(p_box, p_actions[index].p_text);
        style_button(p_btn, p_actions[index], (index + 1U) == used, index > 0U, p_user);
    }
    lv_obj_t* const p_footer = lv_msgbox_get_footer(p_box);
    if (p_footer != nullptr) {
        lv_obj_set_style_bg_opa(p_footer, LV_OPA_TRANSP, 0);
        lv_obj_set_style_pad_all(p_footer, 0, 0);
        lv_obj_set_style_pad_column(p_footer, 0, 0);
        lv_obj_set_height(p_footer, theme::px(BUTTON_HEIGHT));
        lv_obj_set_style_border_side(p_footer, LV_BORDER_SIDE_TOP, 0);
        lv_obj_set_style_border_width(p_footer, theme::px(SEPARATOR_WIDTH), 0);
        lv_obj_set_style_border_color(p_footer, lv_color_hex(COLOR_SEPARATOR), 0);
    }
    lv_obj_center(p_box);
    return p_box;
}

/** @brief Dạng hay dùng: [Hủy | Hành động]. */
inline lv_obj_t* show(const char* p_title, const char* p_message, const Action& cancel, const Action& confirm,
                      void* p_user)
{
    const Action actions[MAX_ACTIONS] = {cancel, confirm};
    return show(p_title, p_message, actions, MAX_ACTIONS, p_user);
}

} // namespace alert

// ============================================================================
// Bàn phím ảo & ô nhập
// ============================================================================
namespace keyboard {

constexpr uint32_t COLOR_BG = palette::SURFACE;
constexpr uint32_t COLOR_KEY = 0x4A4A4CU;         /**< Phím chữ. */
constexpr uint32_t COLOR_KEY_SPECIAL = 0x2F2F31U; /**< Phím chức năng (shift, xóa, OK...). */
constexpr uint32_t COLOR_KEY_PRESSED = 0x6E6E72U;
constexpr int32_t HEIGHT = 250; /**< dp: bàn phím gắn đáy, cao cố định (kiểu iPad). */
constexpr int32_t PAD = 8;
constexpr int32_t KEY_GAP = 7;
constexpr int32_t KEY_RADIUS = 8;
constexpr int32_t FIELD_PAD = 12;
constexpr int32_t FIELD_RADIUS = 10;

/** @brief Kiểu bàn phím tối kiểu iOS cho lv_keyboard. */
inline void style(lv_obj_t* p_kb)
{
    constexpr auto ITEMS = static_cast<lv_style_selector_t>(LV_PART_ITEMS);
    lv_obj_set_style_bg_color(p_kb, lv_color_hex(COLOR_BG), LV_PART_MAIN);
    lv_obj_set_style_bg_opa(p_kb, LV_OPA_COVER, LV_PART_MAIN);
    lv_obj_set_style_border_width(p_kb, 0, LV_PART_MAIN);
    lv_obj_set_style_radius(p_kb, 0, LV_PART_MAIN);
    lv_obj_set_style_pad_all(p_kb, theme::px(PAD), LV_PART_MAIN);
    lv_obj_set_style_pad_row(p_kb, theme::px(KEY_GAP), LV_PART_MAIN);
    lv_obj_set_style_pad_column(p_kb, theme::px(KEY_GAP), LV_PART_MAIN);

    lv_obj_set_style_bg_color(p_kb, lv_color_hex(COLOR_KEY), ITEMS);
    lv_obj_set_style_bg_opa(p_kb, LV_OPA_COVER, ITEMS);
    lv_obj_set_style_radius(p_kb, theme::px(KEY_RADIUS), ITEMS);
    lv_obj_set_style_border_width(p_kb, 0, ITEMS);
    lv_obj_set_style_shadow_width(p_kb, 0, ITEMS);
    lv_obj_set_style_text_color(p_kb, lv_color_white(), ITEMS);
    lv_obj_set_style_text_font(p_kb, theme::font_medium(), ITEMS);
    // Phím chức năng mang cờ CHECKED trong bản đồ phím của LVGL -> tô tối hơn như iOS.
    lv_obj_set_style_bg_color(p_kb, lv_color_hex(COLOR_KEY_SPECIAL), ITEMS | LV_STATE_CHECKED);
    lv_obj_set_style_text_color(p_kb, lv_color_white(), ITEMS | LV_STATE_CHECKED);
    lv_obj_set_style_bg_color(p_kb, lv_color_hex(COLOR_KEY_PRESSED), ITEMS | LV_STATE_PRESSED);
}

/** @brief Kiểu ô nhập một dòng (lv_textarea) đi cùng bàn phím. */
inline void style_field(lv_obj_t* p_field)
{
    lv_obj_set_style_bg_color(p_field, lv_color_hex(palette::SURFACE), 0);
    lv_obj_set_style_bg_opa(p_field, LV_OPA_COVER, 0);
    lv_obj_set_style_radius(p_field, theme::px(FIELD_RADIUS), 0);
    lv_obj_set_style_border_width(p_field, 1, 0);
    lv_obj_set_style_border_color(p_field, lv_color_hex(palette::FILL), 0);
    lv_obj_set_style_border_color(p_field, lv_color_hex(palette::ACCENT), LV_STATE_FOCUSED);
    lv_obj_set_style_shadow_width(p_field, 0, 0);
    lv_obj_set_style_pad_all(p_field, theme::px(FIELD_PAD), 0);
    lv_obj_set_style_text_font(p_field, theme::font_medium(), 0);
    lv_obj_set_style_text_color(p_field, lv_color_white(), 0);
    lv_obj_set_style_text_color(p_field, lv_color_hex(palette::LABEL_SECONDARY), LV_PART_TEXTAREA_PLACEHOLDER);
    lv_obj_set_style_border_color(p_field, lv_color_hex(palette::ACCENT), LV_PART_CURSOR);
}

} // namespace keyboard

} // namespace fw

#endif /* FRAMEWORK_SYSTEM_UI_HPP */
