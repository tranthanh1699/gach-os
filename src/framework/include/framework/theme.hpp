/**
 * @file    theme.hpp
 * @brief   Bảng màu, font và kích thước dùng chung để mọi app có giao diện nhất quán, co giãn theo màn hình.
 *
 * Mọi kích thước trong code được viết theo **đơn vị thiết kế** (dp) cho màn chuẩn 320x240, rồi đổi sang pixel
 * thật qua px(). Framework gọi init() một lần với độ phân giải màn hình trước khi dựng UI:
 *   - Màn nhỏ (cạnh ngắn < config::UI_LARGE_SCREEN_MIN_SIDE): tỷ lệ 100% (y như thiết kế gốc).
 *   - Màn lớn (máy tính bảng): tỷ lệ config::UI_LARGE_SCREEN_SCALE_PCT; diện tích dư dùng để xếp thêm cột.
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

constexpr int32_t BASE_PAD = 8;     /**< Lề chuẩn (dp). Dùng pad() để lấy pixel thật. */
constexpr int32_t BASE_RADIUS = 10; /**< Bo góc chuẩn (dp). Dùng radius() để lấy pixel thật. */

constexpr int32_t SCALE_100 = 100; /**< Tỷ lệ gốc (phần trăm). */

/** @brief Cỡ chữ ngữ nghĩa (kích thước thật phụ thuộc tỷ lệ màn hình). */
enum class FontSize : uint8_t {
    SMALL,  /**< Chữ thường (14 dp). */
    MEDIUM, /**< Tiêu đề (20 dp). */
    LARGE   /**< Số lớn: đồng hồ, máy tính (28 dp). */
};

/**
 * @brief Tính tỷ lệ và chọn font theo độ phân giải màn hình. Framework gọi 1 lần, trước khi dựng UI.
 * @param[in] screen_width  Chiều rộng logic (pixel).
 * @param[in] screen_height Chiều cao logic (pixel).
 */
void init(int32_t screen_width, int32_t screen_height);

/** @return Tỷ lệ hiện hành (phần trăm, 100 = thiết kế gốc). */
int32_t scale_pct();

/** @return true nếu đang chạy trên màn lớn (máy tính bảng). */
bool is_large_screen();

/**
 * @brief Đổi đơn vị thiết kế (dp) sang pixel thật theo tỷ lệ hiện hành.
 * @param[in] dp Kích thước theo thiết kế 320x240. Giá trị đặc biệt của LVGL (LV_PCT, LV_SIZE_CONTENT...)
 *               KHÔNG được truyền vào đây.
 */
int32_t px(int32_t dp);

/** @return Lề chuẩn (pixel). */
inline int32_t pad() { return px(BASE_PAD); }

/** @return Bo góc chuẩn (pixel). */
inline int32_t radius() { return px(BASE_RADIUS); }

/** @return Font theo cỡ ngữ nghĩa, đã co giãn theo màn hình. */
const lv_font_t* font(FontSize size);

/** @return Font cỡ gốc 14/20/28 px, không co giãn (cho app Lua đời cũ sdk <= 2). */
const lv_font_t* base_font(FontSize size);

/** @return Font nhỏ (chữ thường). */
inline const lv_font_t* font_small() { return font(FontSize::SMALL); }

/** @return Font tiêu đề. */
inline const lv_font_t* font_medium() { return font(FontSize::MEDIUM); }

/** @return Font số lớn – đồng hồ, bấm giờ. */
inline const lv_font_t* font_large() { return font(FontSize::LARGE); }

} // namespace theme
} // namespace fw

#endif /* FRAMEWORK_THEME_HPP */
