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
