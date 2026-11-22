/**
 * @file    board_config.hpp
 * @brief   Sơ đồ chân & hiệu chuẩn của ESP32-2432S028R ("Cheap Yellow Display").
 *
 * Phần cứng: ESP32-D0WD (4 MB flash, không PSRAM), ILI9341 2.8" 240x320 (SPI2/HSPI),
 *            XPT2046 resistive touch (SPI3/VSPI, bus riêng), backlight GPIO21.
 *
 * @note Biến thể CYD khác nhau về hướng panel/touch. Nếu ảnh bị lật -> chỉnh DISPLAY.mirror_*;
 *       nếu chạm lệch -> mở app "Touch" và chỉnh TOUCH.raw_* / invert_* / swap_xy.
 */
#ifndef BOARD_CONFIG_HPP
#define BOARD_CONFIG_HPP

#include <cstddef>
#include <cstdint>

#include "drivers/ili9341.hpp"
#include "drivers/xpt2046.hpp"
#include "esp32_mcal/esp32_pwm.hpp"
#include "esp32_mcal/esp32_spi.hpp"

namespace board {
namespace cfg {

constexpr const char* BOARD_NAME = "ESP32-2432S028R";

constexpr uint16_t SCREEN_WIDTH = 320U; // Landscape.
constexpr uint16_t SCREEN_HEIGHT = 240U;

// ---- LCD: ILI9341 trên HSPI (chân IOMUX native -> chạy được 40 MHz) ----
constexpr esp32_mcal::SpiBusConfig LCD_BUS{
    SPI2_HOST, GPIO_NUM_14 /*SCLK*/, GPIO_NUM_13 /*MOSI*/, GPIO_NUM_NC /*MISO: không đọc LCD*/,
    32U * 1024U /*max transfer*/, true /*DMA*/};
constexpr esp32_mcal::SpiDeviceConfig LCD_DEVICE{GPIO_NUM_15 /*CS*/, 40U * 1000U * 1000U, 0U, true /*half-duplex*/};
constexpr gpio_num_t LCD_DC = GPIO_NUM_2;
// RST của panel nối vào EN của ESP32 -> không có chân reset riêng.

// Bản panel TPM408-2.8 (CYD USB-C) hiển thị đảo màu với INVOFF -> dùng INVON và chỉ lệnh DCS chuẩn
// (tương thích cả ILI9341 lẫn ST7789). Bản CYD gốc ILI9341: invert=false, vendor_init=true.
// Lưu ý khi swap_xy=true: mirror_y lật trục NGANG nhìn thấy, mirror_x lật trục DỌC.
constexpr drivers::Ili9341Config DISPLAY{
    SCREEN_WIDTH, SCREEN_HEIGHT,
    true /*swap_xy*/, false /*mirror_x*/, true /*mirror_y*/, true /*bgr*/, true /*invert*/,
    false /*vendor_init*/};

// ---- Backlight: GPIO21, PWM 5 kHz ----
constexpr esp32_mcal::PwmConfig BACKLIGHT_PWM{GPIO_NUM_21, LEDC_TIMER_0, LEDC_CHANNEL_0, 5000U, LEDC_TIMER_10_BIT};
constexpr bool BACKLIGHT_ACTIVE_HIGH = true;

// ---- Touch: XPT2046 trên VSPI (bus riêng, không DMA, clock thấp) ----
constexpr esp32_mcal::SpiBusConfig TOUCH_BUS{
    SPI3_HOST, GPIO_NUM_25 /*SCLK*/, GPIO_NUM_32 /*MOSI*/, GPIO_NUM_39 /*MISO*/, 64U, false /*no DMA*/};
constexpr esp32_mcal::SpiDeviceConfig TOUCH_DEVICE{GPIO_NUM_33 /*CS*/, 1U * 1000U * 1000U, 0U, false};
// T_IRQ = GPIO36 (input-only, không có pull-up nội) -> không dùng, phát hiện chạm bằng áp lực Z.

constexpr drivers::Xpt2046Config TOUCH{
    SCREEN_WIDTH, SCREEN_HEIGHT,
    200U /*raw_x_min*/, 3700U /*raw_x_max*/, 240U /*raw_y_min*/, 3800U /*raw_y_max*/,
    // Đo trên board TPM408-2.8 với (invert_x=false, invert_y=true): chạm trên-trái -> (319, 39)
    // => X ngược, Y đúng chiều.
    true /*swap_xy*/, true /*invert_x*/, true /*invert_y*/, 300U /*z_threshold*/};

} // namespace cfg
} // namespace board

#endif /* BOARD_CONFIG_HPP */
