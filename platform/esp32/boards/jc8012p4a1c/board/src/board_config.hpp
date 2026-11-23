/**
 * @file    board_config.hpp
 * @brief   Sơ đồ chân & thông số của JC8012P4A1C (Guition 10.1", ESP32-P4).
 *
 * Phần cứng: ESP32-P4 (16 MB flash, PSRAM), panel JD9365 800x1280 MIPI-DSI 2 lane,
 *            cảm ứng điện dung GSL3680 (I2C), ESP32-C6 làm Wi-Fi (esp_hosted SDIO), khe microSD (SDMMC).
 * Nguồn: vendor/esp32_p4_function_ev_board (BSP hãng, cấu hình LCD 1280x800).
 *
 * @note Panel gốc là màn DỌC 800x1280. OS chạy NGANG 1280x800 (kiểu máy tính bảng) nhờ xoay PPA.
 *       Ảnh bị lộn ngược 180° -> đổi SCREEN_ROTATION giữa DEG_90 và DEG_270 (touch tự theo).
 *       Ảnh đúng nhưng chạm bị lật -> chỉnh TOUCH.mirror_x / mirror_y.
 *       Ảnh + chạm cùng lệch 180° (chạm chéo) -> DISPLAY.scan_mirror_x/y phải khớp với TOUCH.mirror_*.
 */
#ifndef BOARD_CONFIG_HPP
#define BOARD_CONFIG_HPP

#include <cstddef>
#include <cstdint>

#include "driver/sdmmc_host.h"
#include "esp32_mcal/esp32_pwm.hpp"
#include "esp32_mcal/esp32_sd_filesystem.hpp"
#include "gsl3680_touch.hpp"
#include "jd9365_display.hpp"
#include "rotation.hpp"

namespace board {
namespace cfg {

constexpr const char* BOARD_NAME = "JC8012P4A1C";

// ---- Panel gốc (dọc) và hướng hiển thị ----
constexpr uint16_t PANEL_WIDTH = 800U;
constexpr uint16_t PANEL_HEIGHT = 1280U;
constexpr Rotation SCREEN_ROTATION = Rotation::DEG_90; // Ngang 1280x800.

// ---- LCD: JD9365, MIPI-DSI 2 lane @ 1500 Mbps, DPI 60 MHz (~60 Hz) ----
// Panel lật quét 180° (GS+SS) giống BSP hãng (esp_lvgl_port rotation.mirror_x/y = true): camera ở cạnh TRÊN
// khi cầm ngang, và là hướng mà cấu hình touch của hãng (mirror_y) được hiệu chỉnh theo.
constexpr Jd9365Config DISPLAY{
    PANEL_WIDTH, PANEL_HEIGHT, SCREEN_ROTATION,
    true /*scan_mirror_x*/, true /*scan_mirror_y*/,
    GPIO_NUM_27 /*reset*/,
    2U /*lanes*/, 1500U /*Mbps/lane*/,
    DpiTiming{60U /*MHz*/, 20U /*HSW*/, 20U /*HBP*/, 40U /*HFP*/, 4U /*VSW*/, 8U /*VBP*/, 20U /*VFP*/},
    3 /*LDO_VO3 -> VDD_MIPI_DPHY*/, 2500 /*mV*/};

// ---- Backlight: GPIO23, PWM 20 kHz (LEDC timer/kênh 1 như BSP) ----
constexpr esp32_mcal::PwmConfig BACKLIGHT_PWM{GPIO_NUM_23, LEDC_TIMER_1, LEDC_CHANNEL_1, 20000U, LEDC_TIMER_10_BIT};
constexpr bool BACKLIGHT_ACTIVE_HIGH = true;

// ---- Touch: GSL3680 trên I2C1 (SDA 7, SCL 8) @ 400 kHz ----
constexpr Gsl3680Config TOUCH{
    I2C_NUM_1, GPIO_NUM_7 /*SDA*/, GPIO_NUM_8 /*SCL*/, 400000U,
    GPIO_NUM_22 /*RST*/, GPIO_NUM_21 /*INT*/,
    PANEL_WIDTH, PANEL_HEIGHT,
    false /*mirror_x*/, true /*mirror_y*/, // Giá trị BSP hãng cho panel 800x1280.
    SCREEN_ROTATION};

// ---- Thẻ microSD: SDMMC slot 0 (IOMUX: CLK43 CMD44 D0-D3 39-42), 4 bit, nguồn từ LDO_VO4 ----
// Slot 1 của cùng controller do esp_hosted dùng cho ESP32-C6 (CLK18 CMD19 D0-D3 14-17, RST 54).
constexpr esp32_mcal::SdmmcConfig SD_CARD{SDMMC_HOST_SLOT_0, 4U, SDMMC_FREQ_HIGHSPEED, 4 /*LDO_VO4*/};

// ---- Vùng RAM cho máy ảo app cài thêm (Lua): PSRAM dư dả ----
constexpr size_t APP_ARENA_BYTES = 1024U * 1024U;

} // namespace cfg
} // namespace board

#endif /* BOARD_CONFIG_HPP */
