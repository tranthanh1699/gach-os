/**
 * @file    ui_lock.hpp
 * @brief   RAII khóa LVGL cho code chạy NGOÀI UI task (task nền, test harness).
 *
 * @note    Callback LVGL và vòng đời app đã chạy trong UI task (đang giữ khóa) – không cần dùng.
 *          Khóa là đệ quy nên lồng nhau trong cùng task vẫn an toàn.
 */
#ifndef FRAMEWORK_UI_LOCK_HPP
#define FRAMEWORK_UI_LOCK_HPP

#include "common/non_copyable.hpp"

namespace fw {

/** @brief Giữ khóa LVGL trong phạm vi khối lệnh. */
class UiLock : private common::NonCopyable {
public:
    UiLock();
    ~UiLock();
};

} // namespace fw

#endif /* FRAMEWORK_UI_LOCK_HPP */
