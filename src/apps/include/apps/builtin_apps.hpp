/**
 * @file    builtin_apps.hpp
 * @brief   Đăng ký bộ ứng dụng tích hợp sẵn vào framework.
 */
#ifndef APPS_BUILTIN_APPS_HPP
#define APPS_BUILTIN_APPS_HPP

#include "common/status.hpp"
#include "framework/app_manager.hpp"

namespace apps {

/**
 * @brief Đăng ký toàn bộ app tích hợp và đặt Launcher làm home.
 * @param[in,out] manager App manager của framework.
 * @return OK hoặc lỗi đăng ký đầu tiên gặp phải.
 */
common::Status register_builtin_apps(fw::AppManager& manager);

} // namespace apps

#endif /* APPS_BUILTIN_APPS_HPP */
