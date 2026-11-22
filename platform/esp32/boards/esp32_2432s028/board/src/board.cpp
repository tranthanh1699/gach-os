/**
 * @file    board.cpp
 * @brief   Composition root của board ESP32-2432S028R: dựng MCAL -> driver -> hal::Devices.
 */
#include "hal_api/board.hpp"

#include <cstdlib>

#include "board_config.hpp"
#include "drivers/ili9341.hpp"
#include "drivers/pwm_backlight.hpp"
#include "drivers/xpt2046.hpp"
#include "esp32_mcal/esp32_gpio.hpp"
#include "esp32_mcal/esp32_pwm.hpp"
#include "esp32_mcal/esp32_spi.hpp"
#include "esp32_mcal/esp32_system.hpp"
#include "esp_log.h"

namespace {

constexpr const char* TAG = "board";

// ---- MCAL instances (cấp phát tĩnh) ----
esp32_mcal::Esp32SpiBus s_lcd_bus;
esp32_mcal::Esp32SpiDevice s_lcd_spi;
esp32_mcal::Esp32GpioOutput s_lcd_dc;
esp32_mcal::Esp32SpiBus s_touch_bus;
esp32_mcal::Esp32SpiDevice s_touch_spi;
esp32_mcal::Esp32PwmChannel s_backlight_pwm;
esp32_mcal::Esp32System s_system;

// ---- Driver instances (tiêm MCAL qua constructor) ----
drivers::Ili9341 s_display{s_lcd_spi, s_lcd_dc, nullptr, board::cfg::DISPLAY};
drivers::Xpt2046 s_touch{s_touch_spi, nullptr, board::cfg::TOUCH};
drivers::PwmBacklight s_backlight{s_backlight_pwm, board::cfg::BACKLIGHT_ACTIVE_HIGH};

hal::Devices s_devices{board::cfg::BOARD_NAME, &s_display, nullptr, nullptr, &s_system};

/** @brief Thiết bị bắt buộc lỗi -> dừng hệ thống (không có màn hình thì OS vô nghĩa). */
void require(common::Status status, const char* p_what)
{
    if (!common::is_ok(status)) {
        ESP_LOGE(TAG, "%s failed (status=%u)", p_what, static_cast<unsigned>(status));
        abort();
    }
}

/** @return true nếu thiết bị tùy chọn init thành công; lỗi chỉ cảnh báo. */
bool optional(common::Status status, const char* p_what)
{
    if (!common::is_ok(status)) {
        ESP_LOGW(TAG, "%s unavailable (status=%u)", p_what, static_cast<unsigned>(status));
        return false;
    }
    return true;
}

} // namespace

namespace hal {

const Devices& board_init()
{
    using namespace board::cfg;

    require(s_lcd_bus.init(LCD_BUS), "LCD SPI bus");
    require(s_lcd_spi.init(s_lcd_bus, LCD_DEVICE), "LCD SPI device");
    require(s_lcd_dc.init(LCD_DC, mcal::PinLevel::HIGH), "LCD DC pin");

    // Backlight tắt (duty 0) cho tới khi framework vẽ xong khung đầu tiên.
    if (optional(s_backlight_pwm.init(BACKLIGHT_PWM), "Backlight PWM")) {
        s_devices.p_backlight = &s_backlight;
    }

    const bool touch_ok = optional(s_touch_bus.init(TOUCH_BUS), "Touch SPI bus") &&
                          optional(s_touch_spi.init(s_touch_bus, TOUCH_DEVICE), "Touch SPI device");
    if (touch_ok) {
        s_devices.p_touch = &s_touch;
    }

    ESP_LOGI(TAG, "%s ready (%ux%u, touch=%s)", BOARD_NAME, static_cast<unsigned>(SCREEN_WIDTH),
             static_cast<unsigned>(SCREEN_HEIGHT), touch_ok ? "yes" : "no");
    return s_devices;
}

} // namespace hal
