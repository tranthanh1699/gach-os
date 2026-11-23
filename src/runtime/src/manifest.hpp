/**
 * @file    manifest.hpp
 * @brief   [Private] Đọc & kiểm tra manifest.ini của gói app (.gapp).
 *
 * Định dạng (UTF-8, mỗi dòng key=value, '#' là comment):
 *   id=com.example.hello      bắt buộc, [a-z0-9._-], <= 31 ký tự (cũng là tên thư mục cài)
 *   name=Hello                bắt buộc, <= 23 ký tự
 *   version=1.0.0             bắt buộc
 *   sdk=1                     bắt buộc, <= runtime::SDK_VERSION
 *   entry=main.lua            tùy chọn (mặc định main.lua)
 *   icon=bell                 tùy chọn (tên ký hiệu, xem icon_symbol())
 *   color=#E3B341             tùy chọn
 *   permissions=storage       tùy chọn, cách nhau bằng dấu phẩy
 */
#ifndef RUNTIME_MANIFEST_HPP
#define RUNTIME_MANIFEST_HPP

#include <cstddef>
#include <cstdint>

#include "common/status.hpp"
#include "runtime/runtime.hpp"

namespace runtime {

constexpr size_t APP_ID_BUF_LEN = PACKAGE_ID_BUF_LEN;
constexpr size_t APP_NAME_BUF_LEN = 24U;
constexpr size_t APP_VERSION_BUF_LEN = 16U;
constexpr size_t APP_ENTRY_BUF_LEN = 32U;
constexpr size_t APP_ICON_BUF_LEN = 16U;

/** @brief Bit quyền truy cập khai báo trong manifest. */
enum Permission : uint8_t {
    PERMISSION_NONE = 0U,
    PERMISSION_STORAGE = 1U << 0U, /**< gach.storage */
    PERMISSION_NETWORK = 1U << 1U  /**< gach.net (SDK v5). */
};

/** @brief Nội dung manifest đã kiểm tra. */
struct Manifest {
    char id[APP_ID_BUF_LEN];
    char name[APP_NAME_BUF_LEN];
    char version[APP_VERSION_BUF_LEN];
    char entry[APP_ENTRY_BUF_LEN];
    char icon[APP_ICON_BUF_LEN];
    uint32_t color_hex;
    uint8_t sdk;
    uint8_t permissions;
};

/**
 * @brief Phân tích manifest từ bộ nhớ.
 * @param[in]  p_text Nội dung (không cần kết thúc '\0').
 * @param[in]  len    Độ dài.
 * @param[out] out    Kết quả.
 * @param[out] p_err  Mô tả lỗi tĩnh (khi trả lỗi).
 */
common::Status parse_manifest(const char* p_text, size_t len, Manifest& out, const char** p_err);

/** @return true nếu id hợp lệ làm tên thư mục. */
bool is_valid_app_id(const char* p_id);

/** @return Ký hiệu LVGL cho tên icon (mặc định: biểu tượng file). */
const char* icon_symbol(const char* p_name);

} // namespace runtime

#endif /* RUNTIME_MANIFEST_HPP */
