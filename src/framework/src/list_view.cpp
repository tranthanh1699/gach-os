/**
 * @file    list_view.cpp
 * @brief   Hiện thực danh sách cuộn (kích thước theo dp, co giãn cùng theme).
 */
#include "framework/list_view.hpp"

#include "framework/theme.hpp"

namespace fw {
namespace list_view {

namespace {

constexpr int32_t ROW_PAD_HOR = 10;    /**< dp */
constexpr int32_t ROW_PAD_VER = 8;     /**< dp */
constexpr int32_t ROW_GAP = 8;         /**< dp – khoảng giữa ký hiệu và chữ. */
constexpr int32_t SEPARATOR_WIDTH = 1; /**< dp */
constexpr uint32_t COLOR_LIST_BG = 0x0D1117U;
constexpr uint32_t COLOR_ROW_PRESSED = 0x21262DU;
constexpr uint32_t COLOR_SEPARATOR = 0x30363DU;

} // namespace

lv_obj_t* create(lv_obj_t* p_parent)
{
    lv_obj_t* const p_list = lv_obj_create(p_parent);
    lv_obj_set_width(p_list, LV_PCT(100));
    lv_obj_set_flex_grow(p_list, 1);
    lv_obj_set_flex_flow(p_list, LV_FLEX_FLOW_COLUMN);
    lv_obj_set_scroll_dir(p_list, LV_DIR_VER);
    lv_obj_set_style_pad_all(p_list, 0, 0);
    lv_obj_set_style_pad_row(p_list, 0, 0);
    lv_obj_set_style_bg_color(p_list, lv_color_hex(COLOR_LIST_BG), 0);
    lv_obj_set_style_radius(p_list, theme::radius(), 0);
    lv_obj_set_style_clip_corner(p_list, false, 0);
    return p_list;
}

lv_obj_t* add(lv_obj_t* p_list, const char* p_symbol, const char* p_text)
{
    lv_obj_t* const p_row = lv_button_create(p_list);
    lv_obj_set_size(p_row, LV_PCT(100), LV_SIZE_CONTENT);
    lv_obj_set_flex_flow(p_row, LV_FLEX_FLOW_ROW);
    lv_obj_set_flex_align(p_row, LV_FLEX_ALIGN_START, LV_FLEX_ALIGN_CENTER, LV_FLEX_ALIGN_CENTER);
    lv_obj_set_style_pad_hor(p_row, theme::px(ROW_PAD_HOR), 0);
    lv_obj_set_style_pad_ver(p_row, theme::px(ROW_PAD_VER), 0);
    lv_obj_set_style_pad_column(p_row, theme::px(ROW_GAP), 0);
    lv_obj_set_style_radius(p_row, 0, 0);
    lv_obj_set_style_shadow_width(p_row, 0, 0);
    lv_obj_set_style_bg_opa(p_row, LV_OPA_TRANSP, 0);
    lv_obj_set_style_bg_opa(p_row, LV_OPA_COVER, LV_STATE_PRESSED);
    lv_obj_set_style_bg_color(p_row, lv_color_hex(COLOR_ROW_PRESSED), LV_STATE_PRESSED);
    lv_obj_set_style_border_side(p_row, LV_BORDER_SIDE_BOTTOM, 0);
    lv_obj_set_style_border_width(p_row, theme::px(SEPARATOR_WIDTH), 0);
    lv_obj_set_style_border_color(p_row, lv_color_hex(COLOR_SEPARATOR), 0);
    lv_obj_set_style_text_color(p_row, lv_color_hex(theme::COLOR_TEXT), 0);
    if (p_symbol != nullptr) {
        lv_obj_t* const p_icon = lv_label_create(p_row);
        lv_label_set_text_static(p_icon, p_symbol);
    }
    lv_obj_t* const p_label = lv_label_create(p_row);
    lv_label_set_text(p_label, p_text);
    lv_label_set_long_mode(p_label, LV_LABEL_LONG_MODE_DOTS);
    lv_obj_set_flex_grow(p_label, 1);
    return p_row;
}

} // namespace list_view
} // namespace fw
