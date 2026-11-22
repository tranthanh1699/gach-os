/**
 * @file    theme.hpp
 * @brief   Bảng màu & font dùng chung để mọi app có giao diện nhất quán.
 */
#ifndef FRAMEWORK_THEME_HPP
#define FRAMEWORK_THEME_HPP

#include <cstdint>

#include "lvgl.h"

namespace fw {
namespace theme {

constexpr uint32_t COLOR_BAR_BG = 0x0B0F14U;     /**< Nền status/nav bar. */
constexpr uint32_t COLOR_SCREEN_BG = 0x161B22U;  /**< Nền vùng nội dung. */
constexpr uint32_t COLOR_TEXT = 0xE6EDF3U;       /**< Chữ chính. */
constexpr uint32_t COLOR_TEXT_MUTED = 0x8B949EU; /**< Chữ phụ. */
constexpr uint32_t COLOR_PRIMARY = 0x2F81F7U;    /**< Màu nhấn chính. */
constexpr uint32_t COLOR_SECONDARY = 0x3FB950U;  /**< Màu nhấn phụ. */
constexpr uint32_t COLOR_DANGER = 0xF85149U;     /**< Hành động nguy hiểm. */

constexpr int32_t PAD_NORMAL = 8;  /**< Lề chuẩn (px). */
constexpr int32_t RADIUS = 10;     /**< Bo góc chuẩn (px). */

/** @return Font nhỏ (14px). */
inline const lv_font_t* font_small() { return &lv_font_montserrat_14; }

/** @return Font tiêu đề (20px). */
inline const lv_font_t* font_medium() { return &lv_font_montserrat_20; }

/** @return Font số lớn (28px) – đồng hồ, bấm giờ. */
inline const lv_font_t* font_large() { return &lv_font_montserrat_28; }

} // namespace theme
} // namespace fw

#endif /* FRAMEWORK_THEME_HPP */
