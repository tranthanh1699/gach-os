/**
 * @file    main.cpp
 * @brief   Entry point ESP-IDF: board -> đăng ký app -> khởi động OS.
 *
 * Đây là composition root của platform – nơi duy nhất nối board, framework và apps.
 */
#include <cstdarg>
#include <cstdio>

#include "apps/builtin_apps.hpp"
#include "esp_log.h"
#include "framework/system.hpp"
#include "hal_api/board.hpp"
#include "runtime/runtime.hpp"

namespace {
constexpr const char* TAG = "main";
constexpr size_t LOG_LINE_MAX = 256U; /**< Hook chạy trên stack của mọi task có log: giữ nhỏ. */

/** @brief Hook ESP_LOG: có máy tính kết nối (tool gach) -> gửi qua debug bridge, không thì in chữ thô. */
int bridge_vprintf(const char* p_fmt, va_list args)
{
    char line[LOG_LINE_MAX];
    const int written = std::vsnprintf(line, sizeof(line), p_fmt, args);
    if (written <= 0) {
        return written;
    }
    const size_t len = (static_cast<size_t>(written) < sizeof(line)) ? static_cast<size_t>(written) : (sizeof(line) - 1U);
    if (!runtime::bridge_log(line, len)) {
        (void)std::fwrite(line, 1U, len, stdout);
    }
    return written;
}
} // namespace

extern "C" void app_main(void)
{
    const hal::Devices& devices = hal::board_init();

    common::Status status = apps::register_builtin_apps(fw::app_manager());
    if (common::is_ok(status)) {
        // App cài thêm (thẻ SD): lỗi không chặn khởi động OS.
        const common::Status rt_status = runtime::init(devices, fw::app_manager());
        ESP_LOGI(TAG, "Runtime: %s, %u package(s)%s%s", common::is_ok(rt_status) ? "ready" : "unavailable",
                 static_cast<unsigned>(runtime::package_count()), (runtime::last_error()[0] != '\0') ? " - " : "",
                 runtime::last_error());

        // Debug bridge: lỗi không chặn khởi động (board không có cổng debug vẫn chạy bình thường).
        if (common::is_ok(runtime::bridge_start(devices))) {
            (void)esp_log_set_vprintf(&bridge_vprintf);
        }

        status = fw::start(devices);
    }
    if (!common::is_ok(status)) {
        ESP_LOGE(TAG, "OS start failed (status=%u)", static_cast<unsigned>(status));
        return;
    }
    ESP_LOGI(TAG, "OS started on %s", devices.p_board_name);
    // app_main return: task main tự xóa, UI task tiếp tục chạy.
}
