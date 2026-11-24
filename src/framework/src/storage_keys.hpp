/**
 * @file    storage_keys.hpp
 * @brief   [Private] Danh sách khóa lưu trữ của framework (tập trung để tránh trùng, <= 15 ký tự).
 */
#ifndef FRAMEWORK_STORAGE_KEYS_HPP
#define FRAMEWORK_STORAGE_KEYS_HPP

namespace fw {
namespace storage_key {

constexpr const char* WIFI_SSID = "wifi_ssid";
constexpr const char* WIFI_PASSWORD = "wifi_pass";
constexpr const char* UTC_OFFSET_MIN = "tz_offset_min";
constexpr const char* APPS_DISABLED = "apps_disabled"; /**< Id các app bị tắt, cách nhau dấu phẩy. */

} // namespace storage_key
} // namespace fw

#endif /* FRAMEWORK_STORAGE_KEYS_HPP */
