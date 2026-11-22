/**
 * @file    main.cpp
 * @brief   Entry point ESP-IDF: board -> đăng ký app -> khởi động OS.
 *
 * Đây là composition root của platform – nơi duy nhất nối board, framework và apps.
 */
#include "apps/builtin_apps.hpp"
#include "esp_log.h"
#include "framework/system.hpp"
#include "hal_api/board.hpp"

namespace {
constexpr const char* TAG = "main";
}

extern "C" void app_main(void)
{
    const hal::Devices& devices = hal::board_init();

    common::Status status = apps::register_builtin_apps(fw::app_manager());
    if (common::is_ok(status)) {
        status = fw::start(devices);
    }
    if (!common::is_ok(status)) {
        ESP_LOGE(TAG, "OS start failed (status=%u)", static_cast<unsigned>(status));
        return;
    }
    ESP_LOGI(TAG, "OS started on %s", devices.p_board_name);
    // app_main return: task main tự xóa, UI task tiếp tục chạy.
}
