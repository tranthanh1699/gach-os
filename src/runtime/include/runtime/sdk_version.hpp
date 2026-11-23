/**
 * @file    sdk_version.hpp
 * @brief   Phiên bản gach SDK mà runtime hỗ trợ. Tăng khi thêm API (tương thích ngược).
 *
 * Gói khai báo `sdk=N` trong manifest; runtime từ chối gói có N > SDK_VERSION.
 * Không bao giờ xóa/đổi nghĩa API đã phát hành – chỉ thêm mới.
 */
#ifndef RUNTIME_SDK_VERSION_HPP
#define RUNTIME_SDK_VERSION_HPP

#include <cstdint>

namespace runtime {

constexpr uint8_t SDK_VERSION = 3U; /**< v3: UI co giãn theo màn hình (dp, font), screen_size(). v2: bố cục & kiểu dáng. */

} // namespace runtime

#endif /* RUNTIME_SDK_VERSION_HPP */
