/**
 * @file    ui_helpers.hpp
 * @brief   [Private] Hàm dựng widget dùng chung giữa các app để giữ giao diện nhất quán.
 */
#ifndef APPS_UI_HELPERS_HPP
#define APPS_UI_HELPERS_HPP

#include <cstdint>

#include "framework/theme.hpp"
#include "lvgl.h"

namespace apps {
namespace ui {

/** @brief Tạo label với font/màu chỉ định. */
inline lv_obj_t* label(lv_obj_t* p_parent, const char* p_text, const lv_font_t* p_font, uint32_t color_hex)
{
    lv_obj_t* const p_label = lv_label_create(p_parent);
    lv_label_set_text(p_label, p_text);
    lv_obj_set_style_text_font(p_label, p_font, 0);
    lv_obj_set_style_text_color(p_label, lv_color_hex(color_hex), 0);
    return p_label;
}

/** @brief Tạo nút có chữ, gắn callback LV_EVENT_CLICKED. */
inline lv_obj_t* button(lv_obj_t* p_parent, const char* p_text, lv_event_cb_t cb, void* p_user_data)
{
    lv_obj_t* const p_btn = lv_button_create(p_parent);
    lv_obj_add_event_cb(p_btn, cb, LV_EVENT_CLICKED, p_user_data);
    lv_obj_t* const p_label = lv_label_create(p_btn);
    lv_label_set_text(p_label, p_text);
    lv_obj_center(p_label);
    return p_btn;
}

/** @brief Biến container gốc của app thành cột flex có padding chuẩn. */
inline void column_layout(lv_obj_t* p_root)
{
    lv_obj_set_style_pad_all(p_root, fw::theme::PAD_NORMAL, 0);
    lv_obj_set_style_pad_row(p_root, fw::theme::PAD_NORMAL, 0);
    lv_obj_set_flex_flow(p_root, LV_FLEX_FLOW_COLUMN);
}

} // namespace ui
} // namespace apps

#endif /* APPS_UI_HELPERS_HPP */
