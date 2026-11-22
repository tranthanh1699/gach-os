/**
 * @file    package_archive.cpp
 * @brief   Hiện thực đọc tar ustar theo luồng (không cần RAM bằng kích thước gói).
 */
#include "package_archive.hpp"

#include <cstdint>
#include <cstring>

#include "common/string_utils.hpp"
#include "fs_utils.hpp"

namespace runtime {

namespace {

constexpr size_t TAR_BLOCK = 512U;
constexpr size_t TAR_NAME_OFFSET = 0U;
constexpr size_t TAR_NAME_LEN = 100U;
constexpr size_t TAR_SIZE_OFFSET = 124U;
constexpr size_t TAR_SIZE_LEN = 12U;
constexpr size_t TAR_TYPE_OFFSET = 156U;
constexpr size_t TAR_MAGIC_OFFSET = 257U;
constexpr size_t TAR_PREFIX_OFFSET = 345U;
constexpr size_t TAR_PREFIX_LEN = 155U;
constexpr char TYPE_FILE = '0';
constexpr char TYPE_FILE_OLD = '\0';
constexpr char TYPE_DIR = '5';
constexpr uint32_t OCTAL_BASE = 8U;

// Buffer dùng chung: runtime chỉ chạy trên một task (UI/khởi động) tại một thời điểm.
uint8_t s_block[TAR_BLOCK];

struct Entry {
    char path[PATH_BUF_LEN];
    uint32_t size;
    bool is_dir;
    bool is_file;
};

/** @brief Đọc chính xác một block 512 byte. @return false nếu hết file/lỗi. */
bool read_block(hal::IFileSystem& fs, hal::FileHandle handle)
{
    size_t total = 0U;
    while (total < TAR_BLOCK) {
        size_t got = 0U;
        if (!common::is_ok(fs.read(handle, &s_block[total], TAR_BLOCK - total, got)) || (got == 0U)) {
            return false;
        }
        total += got;
    }
    return true;
}

uint32_t parse_octal(const uint8_t* p_field, size_t len)
{
    uint32_t value = 0U;
    for (size_t index = 0U; index < len; ++index) {
        const uint8_t ch = p_field[index];
        if ((ch >= '0') && (ch <= '7')) {
            value = (value * OCTAL_BASE) + static_cast<uint32_t>(ch - '0');
        } else if (ch != ' ') {
            break;
        }
    }
    return value;
}

bool is_zero_block()
{
    for (const uint8_t byte : s_block) {
        if (byte != 0U) {
            return false;
        }
    }
    return true;
}

bool is_safe_path(const char* p_path)
{
    if ((p_path[0] == '\0') || (p_path[0] == '/') || (std::strchr(p_path, '\\') != nullptr)) {
        return false;
    }
    // Từ chối mọi thành phần ".." (zip-slip).
    for (const char* p = p_path; *p != '\0'; ++p) {
        const bool at_start = (p == p_path) || (*(p - 1) == '/');
        if (at_start && (p[0] == '.') && (p[1] == '.') && ((p[2] == '/') || (p[2] == '\0'))) {
            return false;
        }
    }
    return true;
}

/** @brief Giải mã header hiện có trong s_block. @return false nếu path không an toàn/quá dài. */
bool decode_header(Entry& entry)
{
    char name[TAR_NAME_LEN + 1U] = {};
    char prefix[TAR_PREFIX_LEN + 1U] = {};
    (void)std::memcpy(name, &s_block[TAR_NAME_OFFSET], TAR_NAME_LEN);
    const bool is_ustar = (std::memcmp(&s_block[TAR_MAGIC_OFFSET], "ustar", 5U) == 0);
    if (is_ustar) {
        (void)std::memcpy(prefix, &s_block[TAR_PREFIX_OFFSET], TAR_PREFIX_LEN);
    }
    char full[PATH_BUF_LEN] = {};
    if (prefix[0] != '\0') {
        if (!join_path(full, sizeof(full), prefix, name)) {
            return false;
        }
    } else if (common::copy_string(full, sizeof(full), name) != std::strlen(name)) {
        return false;
    }

    const char* p_path = full;
    while ((p_path[0] == '.') && (p_path[1] == '/')) {
        p_path += 2; // Bỏ "./" do `tar -C dir .` sinh ra.
    }
    (void)common::copy_string(entry.path, sizeof(entry.path), p_path);
    size_t len = std::strlen(entry.path);
    while ((len > 0U) && (entry.path[len - 1U] == '/')) {
        entry.path[--len] = '\0';
    }

    const char type = static_cast<char>(s_block[TAR_TYPE_OFFSET]);
    entry.size = parse_octal(&s_block[TAR_SIZE_OFFSET], TAR_SIZE_LEN);
    entry.is_dir = (type == TYPE_DIR);
    entry.is_file = (type == TYPE_FILE) || (type == TYPE_FILE_OLD);
    return (len == 0U) || (std::strcmp(entry.path, ".") == 0) || is_safe_path(entry.path);
}

/** @brief Bỏ qua `size` byte dữ liệu + phần đệm tới biên block. */
bool skip_data(hal::IFileSystem& fs, hal::FileHandle handle, uint32_t size)
{
    uint32_t blocks = (size + static_cast<uint32_t>(TAR_BLOCK) - 1U) / static_cast<uint32_t>(TAR_BLOCK);
    while (blocks > 0U) {
        if (!read_block(fs, handle)) {
            return false;
        }
        --blocks;
    }
    return true;
}

/** @brief Chép dữ liệu entry sang file đích theo từng block. */
common::Status copy_data(hal::IFileSystem& fs, hal::FileHandle src, hal::FileHandle dst, uint32_t size)
{
    uint32_t remaining = size;
    while (remaining > 0U) {
        if (!read_block(fs, src)) {
            return common::Status::ERR_IO;
        }
        const size_t chunk = (remaining < TAR_BLOCK) ? remaining : TAR_BLOCK;
        const common::Status status = fs.write(dst, s_block, chunk);
        if (!common::is_ok(status)) {
            return status;
        }
        remaining -= static_cast<uint32_t>(chunk);
    }
    return common::Status::OK;
}

common::Status extract_file(hal::IFileSystem& fs, hal::FileHandle src, const char* p_dest_dir, const Entry& entry)
{
    char target[PATH_BUF_LEN];
    if (!join_path(target, sizeof(target), p_dest_dir, entry.path)) {
        return common::Status::ERR_PARAM;
    }
    char* const p_slash = std::strrchr(target, '/');
    if (p_slash != nullptr) {
        *p_slash = '\0';
        const common::Status status = make_dirs(fs, target);
        *p_slash = '/';
        if (!common::is_ok(status)) {
            return status;
        }
    }
    hal::FileHandle dst = hal::INVALID_FILE;
    common::Status status = fs.open(target, hal::OpenMode::WRITE, dst);
    if (common::is_ok(status)) {
        status = copy_data(fs, src, dst, entry.size);
        fs.close(dst);
    }
    return status;
}

} // namespace

common::Status archive_read_entry(hal::IFileSystem& fs, const char* p_archive, const char* p_entry, char* p_buf,
                                  size_t buf_len, size_t& out_len)
{
    out_len = 0U;
    hal::FileHandle handle = hal::INVALID_FILE;
    common::Status status = fs.open(p_archive, hal::OpenMode::READ, handle);
    if (!common::is_ok(status)) {
        return status;
    }
    status = common::Status::ERR_NOT_FOUND;
    while (read_block(fs, handle) && !is_zero_block()) {
        Entry entry{};
        if (!decode_header(entry)) {
            status = common::Status::ERR_PARAM;
            break;
        }
        if (entry.is_file && (std::strcmp(entry.path, p_entry) == 0)) {
            status = (entry.size > buf_len) ? common::Status::ERR_NO_MEM : common::Status::OK;
            uint32_t remaining = entry.size;
            while (common::is_ok(status) && (remaining > 0U)) {
                if (!read_block(fs, handle)) {
                    status = common::Status::ERR_IO;
                    break;
                }
                const size_t chunk = (remaining < TAR_BLOCK) ? remaining : TAR_BLOCK;
                (void)std::memcpy(&p_buf[out_len], s_block, chunk);
                out_len += chunk;
                remaining -= static_cast<uint32_t>(chunk);
            }
            break;
        }
        if (!skip_data(fs, handle, entry.size)) {
            status = common::Status::ERR_IO;
            break;
        }
    }
    fs.close(handle);
    return status;
}

common::Status archive_extract(hal::IFileSystem& fs, const char* p_archive, const char* p_dest_dir)
{
    hal::FileHandle handle = hal::INVALID_FILE;
    common::Status status = fs.open(p_archive, hal::OpenMode::READ, handle);
    if (!common::is_ok(status)) {
        return status;
    }
    while (common::is_ok(status) && read_block(fs, handle) && !is_zero_block()) {
        Entry entry{};
        if (!decode_header(entry)) {
            status = common::Status::ERR_PARAM;
        } else if ((entry.path[0] == '\0') || (std::strcmp(entry.path, ".") == 0)) {
            status = skip_data(fs, handle, entry.size) ? common::Status::OK : common::Status::ERR_IO;
        } else if (entry.is_dir) {
            char target[PATH_BUF_LEN];
            status = join_path(target, sizeof(target), p_dest_dir, entry.path) ? make_dirs(fs, target)
                                                                                : common::Status::ERR_PARAM;
        } else if (entry.is_file) {
            status = extract_file(fs, handle, p_dest_dir, entry);
        } else {
            // Symlink, pax header...: không hỗ trợ, bỏ qua dữ liệu.
            status = skip_data(fs, handle, entry.size) ? common::Status::OK : common::Status::ERR_IO;
        }
    }
    fs.close(handle);
    return status;
}

} // namespace runtime
