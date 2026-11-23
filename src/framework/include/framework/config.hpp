/**
 * @file    config.hpp
 * @brief   Hằng số cấu hình framework (độc lập board). Không dùng magic number ở nơi khác.
 */
#ifndef FRAMEWORK_CONFIG_HPP
#define FRAMEWORK_CONFIG_HPP

#include <cstddef>
#include <cstdint>

namespace fw {
namespace config {

constexpr size_t MAX_APPS = 160U;     /**< Số app tối đa đăng ký được (tích hợp + cài thêm, >= runtime::MAX_PACKAGES + app tích hợp). */
constexpr size_t MAX_APP_STACK = 4U;  /**< Độ sâu back-stack (bao gồm home). */


constexpr int32_t STATUS_BAR_HEIGHT = 22; /**< Thanh trạng thái phía trên (dp, xem theme::px). */
constexpr int32_t NAV_BAR_HEIGHT = 34;    /**< Thanh điều hướng Back/Home phía dưới (dp). */

constexpr int32_t UI_LARGE_SCREEN_MIN_SIDE = 600;  /**< Cạnh ngắn (px) từ đó coi là màn lớn / máy tính bảng. */
constexpr int32_t UI_LARGE_SCREEN_SCALE_PCT = 150; /**< Tỷ lệ UI trên màn lớn (%): dễ đọc khi cầm xa hơn. */

constexpr uint32_t UI_TASK_STACK_BYTES = 12U * 1024U; /**< Đủ cho init + render SW của LVGL. */
constexpr uint8_t UI_TASK_PRIORITY = 4U;
/**
 * Core chạy UI (LVGL + máy ảo Lua). Core 1: tránh ngắt/task esp_timer và task main (ghim core 0 trên ESP-IDF);
 * việc nền (mạng/TLS, debug bridge) ghim sang core 0. Chip 1 core: osal tự bỏ ghim.
 */
constexpr int8_t UI_TASK_CORE = 1;
constexpr uint32_t UI_START_TIMEOUT_MS = 5000U; /**< fw::start() chờ UI task khởi tạo xong. */
constexpr uint32_t UI_PERIOD_MIN_MS = 5U;  /**< Chu kỳ tối thiểu vòng lặp UI. */
constexpr uint32_t UI_PERIOD_MAX_MS = 30U; /**< Chu kỳ tối đa (đảm bảo indev được poll đều). */

constexpr uint8_t DEFAULT_BRIGHTNESS = 80U; /**< Độ sáng khởi động (%). */
constexpr uint32_t STATUS_REFRESH_MS = 1000U;

constexpr uint32_t NET_POLL_PERIOD_MS = 500U;           /**< Chu kỳ framework đọc trạng thái Wi-Fi. */
constexpr size_t WIFI_MAX_NETWORKS = 16U;               /**< Số mạng tối đa giữ lại sau mỗi lần quét. */
constexpr const char* NTP_SERVER = "time.google.com";   /**< Nguồn giờ chuẩn (Google Public NTP). */
constexpr int32_t DEFAULT_UTC_OFFSET_MIN = 7 * 60;      /**< Múi giờ mặc định: UTC+7 (Việt Nam). */

} // namespace config
} // namespace fw

#endif /* FRAMEWORK_CONFIG_HPP */
