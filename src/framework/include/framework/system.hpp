/**
 * @file    system.hpp
 * @brief   Điểm khởi động OS. Chỉ entry point của platform (main) được gọi các API này.
 */
#ifndef FRAMEWORK_SYSTEM_HPP
#define FRAMEWORK_SYSTEM_HPP

#include "common/status.hpp"
#include "framework/app_manager.hpp"

namespace hal {
struct Devices;
}

namespace fw {

/** @brief Số liệu hiệu năng vẽ của LVGL trong một cửa sổ đo (từ lần reset gần nhất). */
struct PerfStats {
    uint32_t window_ms;   /**< Độ dài cửa sổ đo. */
    uint32_t frames;      /**< Số lần LVGL vẽ lại (refresh có vùng bẩn). */
    uint32_t render_us;   /**< Tổng thời gian vẽ (gồm cả flush, vì flush chạy đồng bộ trong lúc vẽ). */
    uint32_t flush_us;    /**< Tổng thời gian đẩy pixel lên màn hình (IDisplay::draw_bitmap). */
    uint32_t flush_count; /**< Số lần flush (số dải vẽ). */
    uint32_t flushed_px;  /**< Tổng số pixel đã đẩy. */
    uint32_t max_frame_us; /**< Khung chậm nhất. */
};

/**
 * @brief Đọc số liệu hiệu năng. Gọi khi giữ UiLock (hoặc từ task UI).
 * @param[out] out   Số liệu.
 * @param[in]  reset true -> bắt đầu cửa sổ đo mới.
 */
void perf_snapshot(PerfStats& out, bool reset);

/**
 * @brief Đặt lịch benchmark: task UI vẽ lại toàn màn `rounds` lần (vẽ luôn chạy trên stack của task UI).
 *        Gọi khi giữ UiLock; đọc kết quả bằng perf_bench_result() sau khi nhả khóa.
 */
void perf_bench_start(uint32_t rounds);

/**
 * @brief Kết quả benchmark (gọi khi giữ UiLock).
 * @return true nếu đã xong (out hợp lệ), false nếu còn đang chạy / chưa đặt lịch.
 */
bool perf_bench_result(PerfStats& out);

/** @brief Chẩn đoán task UI, đọc KHÔNG cần UiLock (dùng được khi UI đang treo). */
struct UiDiag {
    uint32_t loop_count;     /**< Số vòng lặp UI đã chạy. */
    uint32_t phase;          /**< 0 ngủ, 1 lv_timer_handler, 2 vẽ, 3 flush, 4 đọc touch. */
    uint32_t phase_since_ms; /**< Thời điểm vào phase hiện tại. */
    int32_t flush_x1;        /**< Vùng flush gần nhất (tọa độ logic). */
    int32_t flush_y1;
    int32_t flush_x2;
    int32_t flush_y2;
    uint32_t flush_count;    /**< Số lần flush đã gửi. */
    bool flush_pending;      /**< Flush bất đồng bộ đã gửi mà chưa báo xong. */
};

/** @param[out] out Trạng thái task UI hiện tại. */
void ui_diag(UiDiag& out);

/**
 * @brief Chạm giả (debug bridge `gach input`, test tự động): khi is_active, LVGL đọc điểm này thay cho màn cảm ứng.
 *        An toàn gọi từ task khác (UI đọc ở lần quét touch kế tiếp, ~16 ms).
 * @param[in] is_active  false = trả quyền cho cảm ứng thật.
 * @param[in] x, y       Tọa độ logic (pixel màn hình).
 * @param[in] is_pressed Đang nhấn.
 */
void input_inject(bool is_active, int32_t x, int32_t y, bool is_pressed);

/** @return App manager toàn cục – dùng để đăng ký app trước khi start(). */
AppManager& app_manager();

/** @return Dịch vụ hệ thống toàn cục (thời gian, Wi-Fi, file...) – gọi khi giữ UiLock nếu dùng ngoài UI task. */
Services& services();

/**
 * @brief Khởi động OS: init thiết bị, LVGL, Shell, mở app home, tạo UI task.
 *
 * @param[in] devices Tập thiết bị từ hal::board_init() (phải sống suốt chương trình).
 * @return OK, hoặc lỗi init display/LVGL/tạo task.
 * @note   Gọi đúng 1 lần, sau khi đã đăng ký app và set_home().
 */
common::Status start(const hal::Devices& devices);

} // namespace fw

#endif /* FRAMEWORK_SYSTEM_HPP */
