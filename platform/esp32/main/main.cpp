/**
 * @file    main.cpp
 * @brief   Entry point ESP-IDF: board -> đăng ký app -> khởi động OS.
 *
 * Đây là composition root của platform – nơi duy nhất nối board, framework và apps.
 */
#include <cstdarg>
#include <cstdio>

#include "apps/builtin_apps.hpp"
#include "common/log.hpp"
#include "esp_log.h"
#include "framework/system.hpp"
#include "hal_api/board.hpp"
#include "runtime/runtime.hpp"

namespace {
constexpr const char* TAG = "main";
constexpr size_t LOG_LINE_MAX = 256U; /**< Hook chạy trên stack của mọi task có log: giữ nhỏ. */

/**
 * @brief Đường ra chung của MỌI dòng log (gach log + ESP_LOG): ghi lịch sử (`gach dmesg`), rồi gửi qua debug bridge
 *        nếu máy tính đang kết nối, không thì in chữ thô ra UART.
 */
void emit_line(const char* p_line, size_t len)
{
    runtime::log_history_append(p_line, len);
    if (!runtime::bridge_log(p_line, len)) {
        (void)std::fwrite(p_line, 1U, len, stdout);
    }
}

/** @brief Hook ESP_LOG (log của ESP-IDF, driver, vendor). */
int log_vprintf(const char* p_fmt, va_list args)
{
    char line[LOG_LINE_MAX];
    const int written = std::vsnprintf(line, sizeof(line), p_fmt, args);
    if (written <= 0) {
        return written;
    }
    const size_t len = (static_cast<size_t>(written) < sizeof(line)) ? static_cast<size_t>(written) : (sizeof(line) - 1U);
    emit_line(line, len);
    return written;
}

/** @brief Backend gach log: cùng định dạng với ESP_LOG ("I (12345) tag: nội dung"). */
void log_backend(common::log::Level level, const char* p_tag, const char* p_text, size_t len)
{
    char line[LOG_LINE_MAX];
    const int written = std::snprintf(line, sizeof(line), "%c (%lu) %s: %.*s\n", common::log::level_char(level),
                                      static_cast<unsigned long>(esp_log_timestamp()), p_tag, static_cast<int>(len),
                                      p_text);
    if (written <= 0) {
        return;
    }
    size_t used = (static_cast<size_t>(written) < sizeof(line)) ? static_cast<size_t>(written) : (sizeof(line) - 1U);
    line[used - 1U] = '\n'; // Dòng bị cắt vẫn kết thúc bằng xuống dòng.
    emit_line(line, used);
}
} // namespace

extern "C" void app_main(void)
{
    // Log trước tiên: mọi log từ đây (kể cả board init) vào lịch sử `gach dmesg`.
    common::log::set_backend(&log_backend);
    (void)esp_log_set_vprintf(&log_vprintf);
    runtime::diag_install();

    const hal::Devices& devices = hal::board_init();
    runtime::diag_boot(devices);

    common::Status status = apps::register_builtin_apps(fw::app_manager());
    if (common::is_ok(status)) {
        // App cài thêm (thẻ SD): lỗi không chặn khởi động OS.
        const common::Status rt_status = runtime::init(devices, fw::app_manager());
        ESP_LOGI(TAG, "Runtime: %s, %u package(s)%s%s", common::is_ok(rt_status) ? "ready" : "unavailable",
                 static_cast<unsigned>(runtime::package_count()), (runtime::last_error()[0] != '\0') ? " - " : "",
                 runtime::last_error());

        // Debug bridge: lỗi không chặn khởi động (board không có cổng debug vẫn chạy bình thường).
        (void)runtime::bridge_start(devices);

        status = fw::start(devices);
    }
    if (!common::is_ok(status)) {
        ESP_LOGE(TAG, "OS start failed (status=%u)", static_cast<unsigned>(status));
        return;
    }
    ESP_LOGI(TAG, "OS started on %s", devices.p_board_name);
    // app_main return: task main tự xóa, UI task tiếp tục chạy.
}
