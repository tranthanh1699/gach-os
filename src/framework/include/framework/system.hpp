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
 * @brief Benchmark: vẽ lại toàn màn hình `rounds` lần ngay lập tức, trả số liệu của riêng các lần đó.
 *        Gọi khi giữ UiLock. Chặn UI trong lúc chạy (~rounds x thời gian một khung).
 */
void perf_bench(uint32_t rounds, PerfStats& out);

/** @return App manager toàn cục – dùng để đăng ký app trước khi start(). */
AppManager& app_manager();

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
