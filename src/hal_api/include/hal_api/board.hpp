/**
 * @file    board.hpp
 * @brief   Điểm tiêm phụ thuộc (DI) duy nhất giữa platform và framework.
 *
 * Mỗi board (platform/<chip>/boards/<board>) BẮT BUỘC hiện thực `hal::board_init()`.
 * Framework chỉ nhận `hal::Devices` – không bao giờ biết chip/board cụ thể.
 */
#ifndef HAL_API_BOARD_HPP
#define HAL_API_BOARD_HPP

#include "hal_api/backlight.hpp"
#include "hal_api/clock.hpp"
#include "hal_api/display.hpp"
#include "hal_api/storage.hpp"
#include "hal_api/system.hpp"
#include "hal_api/touch.hpp"
#include "hal_api/wifi.hpp"

namespace hal {

/** @brief Tập thiết bị mà board cung cấp cho OS. Con trỏ NULL = board không có thiết bị đó. */
struct Devices {
    const char* p_board_name; /**< Tên board hiển thị. */
    IDisplay* p_display;      /**< Bắt buộc. */
    ITouch* p_touch;          /**< Tùy chọn. */
    IBacklight* p_backlight;  /**< Tùy chọn. */
    ISystem* p_system;        /**< Bắt buộc. */
    IKeyValueStore* p_storage; /**< Tùy chọn: lưu cài đặt. */
    IWifi* p_wifi;            /**< Tùy chọn. */
    IClock* p_clock;          /**< Tùy chọn: giờ thực (SNTP). */
};

/**
 * @brief Khởi tạo bus/chân/driver của board và trả về tập thiết bị.
 *
 * @return Tham chiếu tới Devices tĩnh (sống suốt vòng đời chương trình).
 * @note   Gọi đúng 1 lần từ entry point của platform, trước khi khởi động framework.
 */
const Devices& board_init();

} // namespace hal

#endif /* HAL_API_BOARD_HPP */
