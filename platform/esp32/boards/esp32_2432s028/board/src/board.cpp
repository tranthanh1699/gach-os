/**
 * @file    board.cpp
 * @brief   Composition root của board ESP32-2432S028R: dựng MCAL -> driver -> hal::Devices.
 */
#include "hal_api/board.hpp"

#include <cstdlib>

#include "board_config.hpp"
#include "drivers/ili9341.hpp"
#include "drivers/pwm_backlight.hpp"
#include "drivers/soft_spi.hpp"
#include "drivers/xpt2046.hpp"
#include "esp32_mcal/esp32_clock.hpp"
#include "esp32_mcal/esp32_gpio.hpp"
#include "esp32_mcal/esp32_nvs_store.hpp"
#include "esp32_mcal/esp32_pwm.hpp"
#include "esp32_mcal/esp32_sd_filesystem.hpp"
#include "esp32_mcal/esp32_spi.hpp"
#include "esp32_mcal/esp32_system.hpp"
#include "esp32_mcal/esp32_wifi.hpp"
#include "esp_heap_caps.h"
#include "esp_log.h"

namespace {

constexpr const char* TAG = "board";
constexpr const char* NVS_NAMESPACE = "gach_os";

// ---- MCAL instances (cấp phát tĩnh) ----
esp32_mcal::Esp32SpiBus s_lcd_bus;
esp32_mcal::Esp32SpiDevice s_lcd_spi;
esp32_mcal::Esp32GpioOutput s_lcd_dc;
esp32_mcal::Esp32GpioOutput s_touch_sclk;
esp32_mcal::Esp32GpioOutput s_touch_mosi;
esp32_mcal::Esp32GpioInput s_touch_miso;
esp32_mcal::Esp32GpioOutput s_touch_cs;
esp32_mcal::Esp32SpiBus s_sd_bus;
esp32_mcal::Esp32SdFileSystem s_sd;
esp32_mcal::Esp32PwmChannel s_backlight_pwm;
esp32_mcal::Esp32System s_system;
esp32_mcal::Esp32NvsStore s_storage;
esp32_mcal::Esp32Wifi s_wifi;
esp32_mcal::Esp32Clock s_clock;

// ---- Driver instances (tiêm MCAL qua constructor) ----
drivers::Ili9341 s_display{s_lcd_spi, s_lcd_dc, nullptr, board::cfg::DISPLAY};
drivers::SoftSpiDevice s_touch_spi{s_touch_sclk, s_touch_mosi, s_touch_miso, s_touch_cs};
drivers::Xpt2046 s_touch{s_touch_spi, nullptr, board::cfg::TOUCH};
drivers::PwmBacklight s_backlight{s_backlight_pwm, board::cfg::BACKLIGHT_ACTIVE_HIGH};

hal::Devices s_devices{board::cfg::BOARD_NAME, &s_display, nullptr, nullptr, &s_system, nullptr, nullptr, nullptr,
                       nullptr, hal::MemoryRegion{nullptr, 0U}};

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

    const bool touch_ok = optional(s_touch_sclk.init(TOUCH_SCLK, mcal::PinLevel::LOW), "Touch SCLK") &&
                          optional(s_touch_mosi.init(TOUCH_MOSI, mcal::PinLevel::LOW), "Touch MOSI") &&
                          optional(s_touch_miso.init(TOUCH_MISO, false), "Touch MISO") &&
                          optional(s_touch_cs.init(TOUCH_CS, mcal::PinLevel::HIGH), "Touch CS");
    if (touch_ok) {
        s_devices.p_touch = &s_touch;
    }

    // Thẻ SD tùy chọn: không có thẻ thì OS vẫn chạy, chỉ thiếu app cài thêm.
    const bool sd_ok = optional(s_sd_bus.init(SD_BUS), "SD SPI bus") && optional(s_sd.mount(SD_BUS.host, SD_CS), "SD card");
    if (sd_ok) {
        s_devices.p_fs = &s_sd;
    }

    // Arena cấp TRƯỚC Wi-Fi để chắc chắn còn một khối RAM liên tục đủ lớn.
    void* const p_arena = heap_caps_malloc(APP_ARENA_BYTES, MALLOC_CAP_8BIT);
    if (p_arena != nullptr) {
        s_devices.app_arena = hal::MemoryRegion{p_arena, APP_ARENA_BYTES};
    } else {
        ESP_LOGW(TAG, "App arena (%u B) unavailable", static_cast<unsigned>(APP_ARENA_BYTES));
    }

    // NVS trước Wi-Fi: Wi-Fi driver cần NVS để lưu dữ liệu hiệu chuẩn RF.
    const bool storage_ok = optional(s_storage.init(NVS_NAMESPACE), "NVS storage");
    if (storage_ok) {
        s_devices.p_storage = &s_storage;
    }
    const bool wifi_ok = storage_ok && optional(s_wifi.init(), "Wi-Fi");
    if (wifi_ok) {
        s_devices.p_wifi = &s_wifi;
        s_devices.p_clock = &s_clock; // SNTP cần netif do Wi-Fi khởi tạo.
    }

    ESP_LOGI(TAG, "%s ready (%ux%u, touch=%s, sd=%s, wifi=%s)", BOARD_NAME, static_cast<unsigned>(SCREEN_WIDTH),
             static_cast<unsigned>(SCREEN_HEIGHT), touch_ok ? "yes" : "no", sd_ok ? "yes" : "no",
             wifi_ok ? "yes" : "no");
    return s_devices;
}

} // namespace hal
