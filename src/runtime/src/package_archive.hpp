/**
 * @file    package_archive.hpp
 * @brief   [Private] Đọc gói .gapp (định dạng tar ustar chuẩn, không nén).
 *
 * Dùng tar chuẩn để có thể đóng gói bằng mọi công cụ (tools/gapp_pack.py, `tar --format=ustar`).
 * An toàn: từ chối đường dẫn tuyệt đối hoặc chứa ".." (chống ghi đè ngoài thư mục app).
 */
#ifndef RUNTIME_PACKAGE_ARCHIVE_HPP
#define RUNTIME_PACKAGE_ARCHIVE_HPP

#include <cstddef>

#include "common/status.hpp"
#include "hal_api/filesystem.hpp"

namespace runtime {

/**
 * @brief Đọc nguyên nội dung một file trong gói (VD: manifest.ini).
 * @param[out] out_len Số byte đã đọc.
 * @return ERR_NOT_FOUND nếu gói không chứa file; ERR_NO_MEM nếu lớn hơn buffer.
 */
common::Status archive_read_entry(hal::IFileSystem& fs, const char* p_archive, const char* p_entry, char* p_buf,
                                  size_t buf_len, size_t& out_len);

/** @brief Giải nén toàn bộ gói vào thư mục đích (đã tồn tại). */
common::Status archive_extract(hal::IFileSystem& fs, const char* p_archive, const char* p_dest_dir);

} // namespace runtime

#endif /* RUNTIME_PACKAGE_ARCHIVE_HPP */
