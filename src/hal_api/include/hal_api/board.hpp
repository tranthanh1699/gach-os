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
#include "hal_api/debug_port.hpp"
#include "hal_api/display.hpp"
#include "hal_api/filesystem.hpp"
#include "hal_api/http.hpp"
#include "hal_api/storage.hpp"
#include "hal_api/system.hpp"
#include "hal_api/touch.hpp"
#include "hal_api/wifi.hpp"

namespace hal {

/** @brief Vùng nhớ liên tục do board cấp (board chọn RAM nội hay PSRAM tùy phần cứng). */
struct MemoryRegion {
    void* p_base;
    size_t size;
};

/** @brief Số buffer vẽ tối đa (2 = double buffering: vẽ buffer này trong lúc đẩy buffer kia lên màn hình). */
constexpr size_t DRAW_BUFFER_COUNT = 2U;

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
    IFileSystem* p_fs;        /**< Tùy chọn: bộ nhớ ngoài chứa app cài thêm (thẻ SD). */
    MemoryRegion app_arena;   /**< Vùng RAM dành riêng cho máy ảo app cài thêm ({nullptr,0} = không hỗ trợ). */
    IDebugPort* p_debug_port; /**< Tùy chọn: cổng debug bridge tới máy tính (tool `gach`). */
    /**
     * Buffer vẽ của giao diện (RGB565, căn 4 byte). Bắt buộc buffer 0; buffer 1 tùy chọn (double buffering, chỉ
     * có lợi khi IDisplay hỗ trợ flush bất đồng bộ). Càng lớn càng ít lần chia nhỏ màn hình khi vẽ lại.
     */
    MemoryRegion draw_buffers[DRAW_BUFFER_COUNT];
    IHttpClient* p_http; /**< Tùy chọn: HTTP(S) cho app (cần mạng, VD Wi-Fi). */
    /**
     * Kho ảnh / bộ đệm đồ họa của app cài thêm đang chạy (ảnh .gimg, canvas, sprite sheet). Thu hồi toàn bộ khi app
     * đóng. {nullptr,0} = không hỗ trợ ảnh (app vẫn chạy, ui.image báo lỗi).
     */
    MemoryRegion asset_arena;
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
