/**
 * @file    fs_utils.cpp
 * @brief   Hiện thực tiện ích filesystem.
 */
#include "fs_utils.hpp"

#include <cstdio>
#include <cstring>

#include "common/string_utils.hpp"

namespace runtime {

namespace {

constexpr uint8_t MAX_TREE_DEPTH = 4U;
constexpr size_t REMOVE_BATCH = 8U;

struct RemoveBatch {
    hal::DirEntry entries[REMOVE_BATCH];
    size_t count;
};

bool collect_entry(const hal::DirEntry& entry, void* p_ctx)
{
    auto* const p_batch = static_cast<RemoveBatch*>(p_ctx);
    p_batch->entries[p_batch->count++] = entry;
    return p_batch->count < REMOVE_BATCH; // Đầy batch -> dừng, xóa xong rồi liệt kê lại.
}

common::Status remove_tree_depth(hal::IFileSystem& fs, const char* p_path, uint8_t depth)
{
    bool is_dir = false;
    if (!fs.exists(p_path, is_dir)) {
        return common::Status::OK;
    }
    if (!is_dir) {
        return fs.remove(p_path);
    }
    if (depth >= MAX_TREE_DEPTH) {
        return common::Status::ERR_NOT_SUPPORTED;
    }
    for (;;) {
        RemoveBatch batch{};
        common::Status status = fs.list_dir(p_path, &collect_entry, &batch);
        if (!common::is_ok(status)) {
            return status;
        }
        if (batch.count == 0U) {
            break;
        }
        for (size_t index = 0U; index < batch.count; ++index) {
            char child[PATH_BUF_LEN];
            if (!join_path(child, sizeof(child), p_path, batch.entries[index].name)) {
                return common::Status::ERR_PARAM;
            }
            status = remove_tree_depth(fs, child, static_cast<uint8_t>(depth + 1U));
            if (!common::is_ok(status)) {
                return status;
            }
        }
    }
    return fs.remove(p_path);
}

} // namespace

bool join_path(char* p_out, size_t out_len, const char* p_dir, const char* p_name)
{
    const int written = std::snprintf(p_out, out_len, "%s/%s", p_dir, p_name);
    return (written > 0) && (static_cast<size_t>(written) < out_len);
}

common::Status make_dirs(hal::IFileSystem& fs, const char* p_path)
{
    char partial[PATH_BUF_LEN];
    if (common::copy_string(partial, sizeof(partial), p_path) != std::strlen(p_path)) {
        return common::Status::ERR_PARAM;
    }
    for (size_t index = 1U; partial[index] != '\0'; ++index) {
        if (partial[index] == '/') {
            partial[index] = '\0';
            const common::Status status = fs.make_dir(partial);
            partial[index] = '/';
            if (!common::is_ok(status)) {
                return status;
            }
        }
    }
    return fs.make_dir(partial);
}

common::Status remove_tree(hal::IFileSystem& fs, const char* p_path)
{
    return remove_tree_depth(fs, p_path, 0U);
}

common::Status read_small_file(hal::IFileSystem& fs, const char* p_path, char* p_buf, size_t buf_len, size_t& out_len)
{
    out_len = 0U;
    hal::FileHandle handle = hal::INVALID_FILE;
    common::Status status = fs.open(p_path, hal::OpenMode::READ, handle);
    if (!common::is_ok(status)) {
        return status;
    }
    for (;;) {
        size_t chunk = 0U;
        if (out_len >= buf_len) {
            uint8_t probe = 0U;
            status = fs.read(handle, &probe, 1U, chunk);
            status = (common::is_ok(status) && (chunk > 0U)) ? common::Status::ERR_NO_MEM : status;
            break;
        }
        status = fs.read(handle, reinterpret_cast<uint8_t*>(&p_buf[out_len]), buf_len - out_len, chunk);
        if (!common::is_ok(status) || (chunk == 0U)) {
            break;
        }
        out_len += chunk;
    }
    fs.close(handle);
    return status;
}

} // namespace runtime
