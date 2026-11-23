/**
 * @file    theme.cpp
 * @brief   Tính tỷ lệ UI theo màn hình và chọn font gần nhất trong số font đã biên dịch.
 *
 * Font LVGL là dữ liệu tĩnh trong flash, bật/tắt từng cỡ qua sdkconfig (CONFIG_LV_FONT_MONTSERRAT_N).
 * Board màn nhỏ chỉ bật 14/20/28; board màn lớn bật thêm cỡ lớn -> theme tự dùng khi có.
 */
#include "framework/theme.hpp"

#include <cstddef>

#include "framework/config.hpp"

namespace fw {
namespace theme {

namespace {

constexpr int32_t BASE_SIZE_SMALL = 14;
constexpr int32_t BASE_SIZE_MEDIUM = 20;
constexpr int32_t BASE_SIZE_LARGE = 28;

/** @brief Một cỡ font đã biên dịch. */
struct FontEntry {
    int32_t size;
    const lv_font_t* p_font;
};

/** @brief Mọi cỡ Montserrat đang bật trong cấu hình LVGL (tăng dần). */
const FontEntry FONTS[] = {
#if LV_FONT_MONTSERRAT_12
    {12, &lv_font_montserrat_12},
#endif
    {BASE_SIZE_SMALL, &lv_font_montserrat_14}, // Luôn bật (font mặc định).
#if LV_FONT_MONTSERRAT_16
    {16, &lv_font_montserrat_16},
#endif
#if LV_FONT_MONTSERRAT_18
    {18, &lv_font_montserrat_18},
#endif
    {BASE_SIZE_MEDIUM, &lv_font_montserrat_20}, // Luôn bật.
#if LV_FONT_MONTSERRAT_22
    {22, &lv_font_montserrat_22},
#endif
#if LV_FONT_MONTSERRAT_24
    {24, &lv_font_montserrat_24},
#endif
#if LV_FONT_MONTSERRAT_26
    {26, &lv_font_montserrat_26},
#endif
    {BASE_SIZE_LARGE, &lv_font_montserrat_28}, // Luôn bật.
#if LV_FONT_MONTSERRAT_30
    {30, &lv_font_montserrat_30},
#endif
#if LV_FONT_MONTSERRAT_32
    {32, &lv_font_montserrat_32},
#endif
#if LV_FONT_MONTSERRAT_36
    {36, &lv_font_montserrat_36},
#endif
#if LV_FONT_MONTSERRAT_40
    {40, &lv_font_montserrat_40},
#endif
#if LV_FONT_MONTSERRAT_42
    {42, &lv_font_montserrat_42},
#endif
#if LV_FONT_MONTSERRAT_48
    {48, &lv_font_montserrat_48},
#endif
};

int32_t s_scale_pct = SCALE_100;
const lv_font_t* s_fonts[3] = {&lv_font_montserrat_14, &lv_font_montserrat_20, &lv_font_montserrat_28};

int32_t base_size(FontSize size)
{
    switch (size) {
    case FontSize::MEDIUM:
        return BASE_SIZE_MEDIUM;
    case FontSize::LARGE:
        return BASE_SIZE_LARGE;
    case FontSize::SMALL:
    default:
        return BASE_SIZE_SMALL;
    }
}

/** @brief Font gần cỡ yêu cầu nhất; bằng khoảng cách thì chọn cỡ lớn hơn (dễ đọc hơn). */
const lv_font_t* nearest_font(int32_t wanted)
{
    const lv_font_t* p_best = FONTS[0].p_font;
    int32_t best_diff = INT32_MAX;
    for (const FontEntry& entry : FONTS) {
        const int32_t diff = (entry.size > wanted) ? (entry.size - wanted) : (wanted - entry.size);
        if (diff <= best_diff) {
            best_diff = diff;
            p_best = entry.p_font;
        }
    }
    return p_best;
}

} // namespace

void init(int32_t screen_width, int32_t screen_height)
{
    const int32_t short_side = (screen_width < screen_height) ? screen_width : screen_height;
    s_scale_pct = (short_side >= config::UI_LARGE_SCREEN_MIN_SIDE) ? config::UI_LARGE_SCREEN_SCALE_PCT : SCALE_100;
    for (size_t index = 0U; index < (sizeof(s_fonts) / sizeof(s_fonts[0])); ++index) {
        s_fonts[index] = nearest_font(px(base_size(static_cast<FontSize>(index))));
    }
}

int32_t scale_pct()
{
    return s_scale_pct;
}

bool is_large_screen()
{
    return s_scale_pct > SCALE_100;
}

int32_t px(int32_t dp)
{
    // Làm tròn đối xứng quanh 0 (dp âm dùng cho offset căn lề).
    const int32_t half = SCALE_100 / 2;
    const int32_t scaled = dp * s_scale_pct;
    return (scaled >= 0) ? ((scaled + half) / SCALE_100) : -((-scaled + half) / SCALE_100);
}

const lv_font_t* font(FontSize size)
{
    return s_fonts[static_cast<size_t>(size)];
}

const lv_font_t* base_font(FontSize size)
{
    switch (size) {
    case FontSize::MEDIUM:
        return &lv_font_montserrat_20;
    case FontSize::LARGE:
        return &lv_font_montserrat_28;
    case FontSize::SMALL:
    default:
        return &lv_font_montserrat_14;
    }
}

} // namespace theme
} // namespace fw
