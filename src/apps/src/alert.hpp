/**
 * @file    alert.hpp
 * @brief   [Private] Hộp thoại xác nhận kiểu iOS: nền mờ, thẻ bo góc giữa màn hình, tiêu đề + mô tả,
 *          hai nút phẳng chia đôi chân thẻ (Hủy | Hành động).
 *
 * Dựa trên lv_msgbox (modal sẵn có) nhưng thay toàn bộ kiểu dáng; kích thước theo dp (theme::px) nên
 * không còn bé xíu trên màn lớn. Đóng bằng lv_msgbox_close_async(handle) như msgbox thường.
 */
#ifndef APPS_ALERT_HPP
#define APPS_ALERT_HPP

#include <cstdint>

#include "framework/theme.hpp"
#include "lvgl.h"

namespace apps {
namespace alert {

constexpr uint32_t COLOR_CARD = 0x2C2C2EU;       /**< Nền thẻ. */
constexpr uint32_t COLOR_PRESSED = 0x3A3A3CU;    /**< Nút đang nhấn. */
constexpr uint32_t COLOR_SEPARATOR = 0x48484AU;  /**< Đường kẻ chân thẻ / giữa hai nút. */
constexpr uint32_t COLOR_TITLE = 0xFFFFFFU;
constexpr uint32_t COLOR_MESSAGE = 0xAEAEB2U;
constexpr uint32_t COLOR_CANCEL = 0x0A84FFU;     /**< Nút Hủy (màu nhấn hệ thống). */
constexpr uint32_t COLOR_DESTRUCTIVE = 0xFF453AU; /**< Hành động nguy hiểm (gỡ app, khởi động lại...). */
constexpr uint32_t COLOR_DEFAULT = 0x0A84FFU;    /**< Hành động thường. */
constexpr lv_opa_t BACKDROP_OPA = LV_OPA_50;

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

/** @brief Nút phẳng chiếm nửa chân thẻ. */
inline void style_button(lv_obj_t* p_btn, const Action& action, bool is_bold, bool has_left_separator, void* p_user)
{
    lv_obj_remove_style_all(p_btn);
    lv_obj_set_height(p_btn, LV_PCT(100));
    lv_obj_set_flex_grow(p_btn, 1);
    lv_obj_set_style_bg_color(p_btn, lv_color_hex(COLOR_PRESSED), LV_STATE_PRESSED);
    lv_obj_set_style_bg_opa(p_btn, LV_OPA_COVER, LV_STATE_PRESSED);
    if (has_left_separator) {
        lv_obj_set_style_border_side(p_btn, LV_BORDER_SIDE_LEFT, 0);
        lv_obj_set_style_border_width(p_btn, fw::theme::px(SEPARATOR_WIDTH), 0);
        lv_obj_set_style_border_color(p_btn, lv_color_hex(COLOR_SEPARATOR), 0);
    }
    lv_obj_t* const p_label = lv_obj_get_child(p_btn, 0);
    if (p_label != nullptr) {
        lv_obj_set_style_text_color(p_label, lv_color_hex(action.color), 0);
        lv_obj_set_style_text_font(p_label, is_bold ? fw::theme::font_medium() : fw::theme::font_small(), 0);
        lv_obj_center(p_label);
    }
    lv_obj_add_event_cb(p_btn, action.cb, LV_EVENT_CLICKED, p_user);
}

/**
 * @brief Hiện hộp thoại xác nhận.
 *
 * @param[in] p_title   Tiêu đề (đậm, căn giữa).
 * @param[in] p_message Mô tả thêm, nullptr = không có.
 * @param[in] cancel    Nút trái (thường "Cancel").
 * @param[in] confirm   Nút phải (hành động chính, chữ to hơn).
 * @param[in] p_user    user_data cho cả hai callback.
 * @return Handle msgbox (đóng bằng lv_msgbox_close_async).
 */
inline lv_obj_t* show(const char* p_title, const char* p_message, const Action& cancel, const Action& confirm,
                      void* p_user)
{
    lv_obj_t* const p_box = lv_msgbox_create(nullptr); // nullptr -> modal trên top layer, có nền chặn chạm.
    lv_obj_t* const p_backdrop = lv_obj_get_parent(p_box);
    lv_obj_set_style_bg_color(p_backdrop, lv_color_black(), 0);
    lv_obj_set_style_bg_opa(p_backdrop, BACKDROP_OPA, 0);

    lv_obj_set_style_bg_color(p_box, lv_color_hex(COLOR_CARD), 0);
    lv_obj_set_style_bg_opa(p_box, LV_OPA_COVER, 0);
    lv_obj_set_style_radius(p_box, fw::theme::px(RADIUS), 0);
    lv_obj_set_style_clip_corner(p_box, true, 0);
    lv_obj_set_style_border_width(p_box, 0, 0);
    lv_obj_set_style_shadow_width(p_box, 0, 0);
    lv_obj_set_style_pad_all(p_box, 0, 0);
    lv_obj_set_style_pad_row(p_box, 0, 0);
    lv_obj_set_width(p_box, fw::theme::px(WIDTH));

    lv_obj_t* const p_content = lv_msgbox_get_content(p_box);
    lv_obj_set_style_pad_hor(p_content, fw::theme::px(BODY_PAD_HOR), 0);
    lv_obj_set_style_pad_top(p_content, fw::theme::px(BODY_PAD_TOP), 0);
    lv_obj_set_style_pad_bottom(p_content, fw::theme::px(BODY_PAD_BOTTOM), 0);
    lv_obj_set_style_pad_row(p_content, fw::theme::px(BODY_GAP), 0);
    lv_obj_set_flex_flow(p_content, LV_FLEX_FLOW_COLUMN);
    lv_obj_set_flex_align(p_content, LV_FLEX_ALIGN_START, LV_FLEX_ALIGN_CENTER, LV_FLEX_ALIGN_CENTER);

    lv_obj_t* const p_title_label = lv_msgbox_add_text(p_box, p_title);
    lv_obj_set_width(p_title_label, LV_PCT(100));
    lv_obj_set_style_text_align(p_title_label, LV_TEXT_ALIGN_CENTER, 0);
    lv_obj_set_style_text_font(p_title_label, fw::theme::font_medium(), 0);
    lv_obj_set_style_text_color(p_title_label, lv_color_hex(COLOR_TITLE), 0);
    if (p_message != nullptr) {
        lv_obj_t* const p_msg = lv_msgbox_add_text(p_box, p_message);
        lv_obj_set_width(p_msg, LV_PCT(100));
        lv_obj_set_style_text_align(p_msg, LV_TEXT_ALIGN_CENTER, 0);
        lv_obj_set_style_text_font(p_msg, fw::theme::font_small(), 0);
        lv_obj_set_style_text_color(p_msg, lv_color_hex(COLOR_MESSAGE), 0);
    }

    lv_obj_t* const p_cancel = lv_msgbox_add_footer_button(p_box, cancel.p_text);
    lv_obj_t* const p_confirm = lv_msgbox_add_footer_button(p_box, confirm.p_text);
    lv_obj_t* const p_footer = lv_msgbox_get_footer(p_box);
    lv_obj_set_style_bg_opa(p_footer, LV_OPA_TRANSP, 0);
    lv_obj_set_style_pad_all(p_footer, 0, 0);
    lv_obj_set_style_pad_column(p_footer, 0, 0);
    lv_obj_set_height(p_footer, fw::theme::px(BUTTON_HEIGHT));
    lv_obj_set_style_border_side(p_footer, LV_BORDER_SIDE_TOP, 0);
    lv_obj_set_style_border_width(p_footer, fw::theme::px(SEPARATOR_WIDTH), 0);
    lv_obj_set_style_border_color(p_footer, lv_color_hex(COLOR_SEPARATOR), 0);
    style_button(p_cancel, cancel, false, false, p_user);
    style_button(p_confirm, confirm, true, true, p_user);

    lv_obj_center(p_box);
    return p_box;
}

} // namespace alert
} // namespace apps

#endif /* APPS_ALERT_HPP */
