/**
 * @file    board.cpp
 * @brief   Composition root của board JC8012P4A1C: dựng MCAL -> driver -> hal::Devices.
 */
#include "hal_api/board.hpp"

#include "board_config.hpp"
#include "drivers/pwm_backlight.hpp"
#include "esp32_mcal/esp32_clock.hpp"
#include "esp32_mcal/esp32_nvs_store.hpp"
#include "esp32_mcal/esp32_pwm.hpp"
#include "esp32_mcal/esp32_sd_filesystem.hpp"
#include "esp32_mcal/esp32_system.hpp"
#include "esp32_mcal/esp32_wifi.hpp"
#include "esp_heap_caps.h"
#include "esp_log.h"
#include "gsl3680_touch.hpp"
#include "jd9365_display.hpp"

namespace {

constexpr const char* TAG = "board";
constexpr const char* NVS_NAMESPACE = "gach_os";

// ---- MCAL / thiết bị platform (cấp phát tĩnh) ----
esp32_mcal::Esp32PwmChannel s_backlight_pwm;
esp32_mcal::Esp32SdFileSystem s_sd;
esp32_mcal::Esp32System s_system;
esp32_mcal::Esp32NvsStore s_storage;
esp32_mcal::Esp32Wifi s_wifi;
esp32_mcal::Esp32Clock s_clock;

// Panel/touch gắn chặt SDK (MIPI-DSI, PPA, esp_lcd_touch) -> hiện thực ở board, không ở drivers/.
board::Jd9365Display s_display{board::cfg::DISPLAY};
board::Gsl3680Touch s_touch{board::cfg::TOUCH};
i2c_master_bus_handle_t s_i2c_bus = nullptr;
drivers::PwmBacklight s_backlight{s_backlight_pwm, board::cfg::BACKLIGHT_ACTIVE_HIGH};

// Display/touch init bởi framework (fw::start) trong UI task.
hal::Devices s_devices{board::cfg::BOARD_NAME, &s_display, &s_touch, nullptr, &s_system, nullptr, nullptr, nullptr,
                       nullptr, hal::MemoryRegion{nullptr, 0U}};

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

    // Bus I2C1 dùng chung (touch, codec...): tạo một lần ở board, chia cho các thiết bị.
    i2c_master_bus_config_t bus_cfg{};
    bus_cfg.i2c_port = I2C_PORT;
    bus_cfg.sda_io_num = I2C_SDA;
    bus_cfg.scl_io_num = I2C_SCL;
    bus_cfg.clk_source = I2C_CLK_SRC_DEFAULT;
    bus_cfg.glitch_ignore_cnt = I2C_GLITCH_FILTER_CYCLES;
    bus_cfg.flags.enable_internal_pullup = 1U;
    if (i2c_new_master_bus(&bus_cfg, &s_i2c_bus) == ESP_OK) {
        s_touch.attach_bus(s_i2c_bus);
    } else {
        ESP_LOGW(TAG, "I2C bus unavailable: no touch");
        s_devices.p_touch = nullptr;
    }

    // Backlight tắt (duty 0) cho tới khi framework vẽ xong khung đầu tiên.
    if (optional(s_backlight_pwm.init(BACKLIGHT_PWM), "Backlight PWM")) {
        s_devices.p_backlight = &s_backlight;
    }

    // Thẻ SD tùy chọn: không có thẻ thì OS vẫn chạy, chỉ thiếu app cài thêm.
    const bool sd_ok = optional(s_sd.mount_sdmmc(SD_CARD), "SD card");
    if (sd_ok) {
        s_devices.p_fs = &s_sd;
    }

    // Board có PSRAM: arena Lua đặt ở PSRAM, nhường RAM nội cho LVGL/Wi-Fi.
    void* const p_arena = heap_caps_malloc(APP_ARENA_BYTES, MALLOC_CAP_SPIRAM | MALLOC_CAP_8BIT);
    if (p_arena != nullptr) {
        s_devices.app_arena = hal::MemoryRegion{p_arena, APP_ARENA_BYTES};
    } else {
        ESP_LOGW(TAG, "App arena (%u B) unavailable", static_cast<unsigned>(APP_ARENA_BYTES));
    }

    // NVS trước Wi-Fi: Wi-Fi (esp_wifi_remote -> ESP32-C6) cần NVS.
    const bool storage_ok = optional(s_storage.init(NVS_NAMESPACE), "NVS storage");
    if (storage_ok) {
        s_devices.p_storage = &s_storage;
    }
    const bool wifi_ok = storage_ok && optional(s_wifi.init(), "Wi-Fi");
    if (wifi_ok) {
        s_devices.p_wifi = &s_wifi;
        s_devices.p_clock = &s_clock; // SNTP cần netif do Wi-Fi khởi tạo.
    }

    ESP_LOGI(TAG, "%s ready (%ux%u, sd=%s, wifi=%s)", BOARD_NAME, static_cast<unsigned>(s_display.width()),
             static_cast<unsigned>(s_display.height()), sd_ok ? "yes" : "no", wifi_ok ? "yes" : "no");
    return s_devices;
}

} // namespace hal
