/**
 * @file    list_view.hpp
 * @brief   Danh sách cuộn dọc gồm các dòng bấm được (thay lv_list – deprecated từ LVGL 9.6).
 */
#ifndef FRAMEWORK_LIST_VIEW_HPP
#define FRAMEWORK_LIST_VIEW_HPP

#include "lvgl.h"

namespace fw {
namespace list_view {

/**
 * @brief Tạo danh sách: cột flex cuộn dọc, rộng 100%, chiếm phần trống còn lại của cha (flex grow).
 * @return Container danh sách.
 */
lv_obj_t* create(lv_obj_t* p_parent);

/**
 * @brief Thêm một dòng: [ký hiệu] chữ, căn trái, đổi nền khi nhấn.
 * @param[in] p_list   Danh sách từ create().
 * @param[in] p_symbol Ký hiệu LV_SYMBOL_* hoặc nullptr.
 * @param[in] p_text   Chữ của dòng (được sao chép).
 * @return Dòng (nút) – gắn LV_EVENT_CLICKED vào đây.
 */
lv_obj_t* add(lv_obj_t* p_list, const char* p_symbol, const char* p_text);

} // namespace list_view
} // namespace fw

#endif /* FRAMEWORK_LIST_VIEW_HPP */
