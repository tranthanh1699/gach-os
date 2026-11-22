/**
 * @file    fs_utils.hpp
 * @brief   [Private] Tiện ích trên hal::IFileSystem: ghép đường dẫn, tạo/xóa cây thư mục, đọc file nhỏ.
 */
#ifndef RUNTIME_FS_UTILS_HPP
#define RUNTIME_FS_UTILS_HPP

#include <cstddef>
#include <cstdint>

#include "common/status.hpp"
#include "hal_api/filesystem.hpp"

namespace runtime {

constexpr size_t PATH_BUF_LEN = hal::FS_PATH_MAX_LEN + 1U;

/** @brief Ghép "a/b". @return false nếu vượt PATH_BUF_LEN. */
bool join_path(char* p_out, size_t out_len, const char* p_dir, const char* p_name);

/** @brief Tạo thư mục và mọi thư mục cha còn thiếu. */
common::Status make_dirs(hal::IFileSystem& fs, const char* p_path);

/** @brief Xóa đệ quy file/thư mục (tối đa 4 cấp). Không tồn tại -> OK. */
common::Status remove_tree(hal::IFileSystem& fs, const char* p_path);

/**
 * @brief Đọc toàn bộ file nhỏ vào buffer.
 * @return ERR_NO_MEM nếu file lớn hơn buffer.
 */
common::Status read_small_file(hal::IFileSystem& fs, const char* p_path, char* p_buf, size_t buf_len, size_t& out_len);

} // namespace runtime

#endif /* RUNTIME_FS_UTILS_HPP */
