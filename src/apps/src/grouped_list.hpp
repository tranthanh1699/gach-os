/**
 * @file    grouped_list.hpp
 * @brief   [Private] Danh sách nhóm kiểu iOS ("inset grouped"): nhóm bo góc, dòng có icon màu, giá trị, mũi tên ›.
 *
 * Mọi kích thước theo dp (theme::px) -> tự co giãn trên màn lớn.
 */
#ifndef APPS_GROUPED_LIST_HPP
#define APPS_GROUPED_LIST_HPP

#include <cstdint>

#include "framework/theme.hpp"
#include "lvgl.h"

namespace apps {
namespace glist {

// ---- Bảng màu kiểu iOS (dark mode) ----
constexpr uint32_t COLOR_PAGE_BG = 0x000000U;     /**< Nền trang. */
constexpr uint32_t COLOR_GROUP_BG = 0x1C1C1EU;    /**< Nền nhóm. */
constexpr uint32_t COLOR_PRESSED = 0x2C2C2EU;     /**< Dòng đang bị nhấn. */
constexpr uint32_t COLOR_SEPARATOR = 0x38383AU;   /**< Đường kẻ giữa các dòng. */
constexpr uint32_t COLOR_TEXT = 0xFFFFFFU;        /**< Chữ chính. */
constexpr uint32_t COLOR_SECONDARY = 0x8E8E93U;   /**< Chữ phụ / giá trị / tiêu đề nhóm. */
constexpr uint32_t COLOR_ACCENT = 0x0A84FFU;      /**< Màu nhấn (dòng đang chọn trong sidebar). */
constexpr uint32_t COLOR_DESTRUCTIVE = 0xFF453AU; /**< Hành động nguy hiểm. */

// ---- Kích thước (dp) ----
constexpr int32_t GROUP_RADIUS = 10;
constexpr int32_t ROW_MIN_HEIGHT = 36;
constexpr int32_t ROW_PAD_HOR = 10;
constexpr int32_t ROW_PAD_VER = 6;
constexpr int32_t ICON_SIZE = 24;
constexpr int32_t ICON_RADIUS = 6;
constexpr int32_t ROW_GAP = 10;
constexpr int32_t SECTION_GAP = 18;
constexpr int32_t HEADER_PAD_LEFT = 12;
constexpr int32_t SEPARATOR_WIDTH = 1;

/** @brief Trang cuộn dọc nền đen, xếp các nhóm theo cột. */
inline lv_obj_t* page(lv_obj_t* p_parent)
{
    lv_obj_t* const p_page = lv_obj_create(p_parent);
    lv_obj_remove_style_all(p_page);
    lv_obj_set_style_bg_color(p_page, lv_color_hex(COLOR_PAGE_BG), 0);
    lv_obj_set_style_bg_opa(p_page, LV_OPA_COVER, 0);
    lv_obj_set_style_pad_all(p_page, fw::theme::pad() * 2, 0);
    lv_obj_set_style_pad_row(p_page, fw::theme::px(SECTION_GAP / 3), 0);
    lv_obj_set_flex_flow(p_page, LV_FLEX_FLOW_COLUMN);
    lv_obj_set_scroll_dir(p_page, LV_DIR_VER);
    lv_obj_set_scrollbar_mode(p_page, LV_SCROLLBAR_MODE_ACTIVE);
    return p_page;
}

/** @brief Tiêu đề lớn của trang (VD "Settings"). */
inline lv_obj_t* large_title(lv_obj_t* p_page, const char* p_text)
{
    lv_obj_t* const p_label = lv_label_create(p_page);
    lv_label_set_text(p_label, p_text);
    lv_obj_set_style_text_font(p_label, fw::theme::font_large(), 0);
    lv_obj_set_style_text_color(p_label, lv_color_hex(COLOR_TEXT), 0);
    lv_obj_set_style_pad_left(p_label, fw::theme::px(HEADER_PAD_LEFT / 2), 0);
    return p_label;
}

/** @brief Tiêu đề nhỏ phía trên một nhóm ("ABOUT", "NETWORK"...); nullptr = chỉ tạo khoảng cách. */
inline void section_header(lv_obj_t* p_page, const char* p_text)
{
    lv_obj_t* const p_label = lv_label_create(p_page);
    lv_label_set_text(p_label, (p_text != nullptr) ? p_text : "");
    lv_obj_set_style_text_color(p_label, lv_color_hex(COLOR_SECONDARY), 0);
    lv_obj_set_style_pad_left(p_label, fw::theme::px(HEADER_PAD_LEFT), 0);
    lv_obj_set_style_pad_top(p_label, fw::theme::px(SECTION_GAP / 2), 0);
}

/** @brief Nhóm bo góc chứa các dòng. */
inline lv_obj_t* group(lv_obj_t* p_page)
{
    lv_obj_t* const p_group = lv_obj_create(p_page);
    lv_obj_remove_style_all(p_group);
    lv_obj_set_size(p_group, LV_PCT(100), LV_SIZE_CONTENT);
    lv_obj_set_style_bg_color(p_group, lv_color_hex(COLOR_GROUP_BG), 0);
    lv_obj_set_style_bg_opa(p_group, LV_OPA_COVER, 0);
    lv_obj_set_style_radius(p_group, fw::theme::px(GROUP_RADIUS), 0);
    lv_obj_set_style_clip_corner(p_group, true, 0);
    lv_obj_set_flex_flow(p_group, LV_FLEX_FLOW_COLUMN);
    lv_obj_remove_flag(p_group, LV_OBJ_FLAG_SCROLLABLE);
    return p_group;
}

/**
 * @brief Một dòng trong nhóm: [icon] Tiêu đề ........ giá trị ›
 *
 * @param[in] p_group     Nhóm chứa (dòng thứ 2 trở đi tự có đường kẻ phía trên).
 * @param[in] p_symbol    Ký hiệu LV_SYMBOL_* trong icon vuông màu, nullptr = không có icon.
 * @param[in] icon_color  Màu nền icon.
 * @param[in] p_title     Tiêu đề.
 * @param[in] cb          Callback khi chạm (nullptr = dòng chỉ hiển thị, không có mũi tên).
 * @param[in] p_user_data Dữ liệu cho callback.
 * @return Label giá trị bên phải (để cập nhật sau); rỗng ban đầu.
 */
inline lv_obj_t* row(lv_obj_t* p_group, const char* p_symbol, uint32_t icon_color, const char* p_title, lv_event_cb_t cb,
                     void* p_user_data)
{
    lv_obj_t* const p_row = lv_obj_create(p_group);
    lv_obj_remove_style_all(p_row);
    lv_obj_set_size(p_row, LV_PCT(100), LV_SIZE_CONTENT);
    lv_obj_set_style_min_height(p_row, fw::theme::px(ROW_MIN_HEIGHT), 0);
    lv_obj_set_style_pad_hor(p_row, fw::theme::px(ROW_PAD_HOR), 0);
    lv_obj_set_style_pad_ver(p_row, fw::theme::px(ROW_PAD_VER), 0);
    lv_obj_set_style_pad_column(p_row, fw::theme::px(ROW_GAP), 0);
    lv_obj_set_flex_flow(p_row, LV_FLEX_FLOW_ROW);
    lv_obj_set_flex_align(p_row, LV_FLEX_ALIGN_START, LV_FLEX_ALIGN_CENTER, LV_FLEX_ALIGN_CENTER);
    lv_obj_remove_flag(p_row, LV_OBJ_FLAG_SCROLLABLE);
    if (lv_obj_get_index(p_row) > 0) {
        lv_obj_set_style_border_side(p_row, LV_BORDER_SIDE_TOP, 0);
        lv_obj_set_style_border_width(p_row, fw::theme::px(SEPARATOR_WIDTH), 0);
        lv_obj_set_style_border_color(p_row, lv_color_hex(COLOR_SEPARATOR), 0);
    }
    if (cb != nullptr) {
        lv_obj_add_flag(p_row, LV_OBJ_FLAG_CLICKABLE);
        lv_obj_set_style_bg_color(p_row, lv_color_hex(COLOR_PRESSED), LV_STATE_PRESSED);
        lv_obj_set_style_bg_opa(p_row, LV_OPA_COVER, LV_STATE_PRESSED);
        lv_obj_add_event_cb(p_row, cb, LV_EVENT_CLICKED, p_user_data);
    }

    if (p_symbol != nullptr) {
        lv_obj_t* const p_icon = lv_obj_create(p_row);
        lv_obj_remove_style_all(p_icon);
        lv_obj_set_size(p_icon, fw::theme::px(ICON_SIZE), fw::theme::px(ICON_SIZE));
        lv_obj_set_style_radius(p_icon, fw::theme::px(ICON_RADIUS), 0);
        lv_obj_set_style_bg_color(p_icon, lv_color_hex(icon_color), 0);
        lv_obj_set_style_bg_opa(p_icon, LV_OPA_COVER, 0);
        lv_obj_remove_flag(p_icon, LV_OBJ_FLAG_CLICKABLE);
        lv_obj_t* const p_glyph = lv_label_create(p_icon);
        lv_label_set_text_static(p_glyph, p_symbol);
        lv_obj_set_style_text_color(p_glyph, lv_color_white(), 0);
        lv_obj_center(p_glyph);
    }

    lv_obj_t* const p_title_label = lv_label_create(p_row);
    lv_label_set_text(p_title_label, p_title);
    lv_obj_set_style_text_color(p_title_label, lv_color_hex(COLOR_TEXT), 0);

    lv_obj_t* const p_value = lv_label_create(p_row);
    lv_label_set_text(p_value, "");
    lv_obj_set_flex_grow(p_value, 1);
    lv_obj_set_style_text_align(p_value, LV_TEXT_ALIGN_RIGHT, 0);
    lv_obj_set_style_text_color(p_value, lv_color_hex(COLOR_SECONDARY), 0);
    lv_label_set_long_mode(p_value, LV_LABEL_LONG_DOT);

    if (cb != nullptr) {
        lv_obj_t* const p_chevron = lv_label_create(p_row);
        lv_label_set_text_static(p_chevron, LV_SYMBOL_RIGHT);
        lv_obj_set_style_text_color(p_chevron, lv_color_hex(COLOR_SECONDARY), 0);
    }
    return p_value;
}

/** @brief Dòng hành động chữ màu, căn giữa (VD "Restart" màu đỏ). Trả về chính dòng. */
inline lv_obj_t* action_row(lv_obj_t* p_group, const char* p_text, uint32_t color, lv_event_cb_t cb, void* p_user_data)
{
    lv_obj_t* const p_value = row(p_group, nullptr, 0U, "", cb, p_user_data);
    lv_obj_t* const p_row = lv_obj_get_parent(p_value);
    lv_obj_set_flex_align(p_row, LV_FLEX_ALIGN_CENTER, LV_FLEX_ALIGN_CENTER, LV_FLEX_ALIGN_CENTER);
    lv_obj_clean(p_row); // Bỏ tiêu đề/giá trị/mũi tên mặc định, chỉ giữ một nhãn căn giữa.
    lv_obj_t* const p_label = lv_label_create(p_row);
    lv_label_set_text(p_label, p_text);
    lv_obj_set_style_text_color(p_label, lv_color_hex(color), 0);
    return p_row;
}

} // namespace glist
} // namespace apps

#endif /* APPS_GROUPED_LIST_HPP */
