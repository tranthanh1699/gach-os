/**
 * @file    file_service.cpp
 * @brief   Hiện thực fw::FileService.
 *
 * Job chép / xóa duyệt cây KHÔNG đệ quy: một cặp bộ đệm đường dẫn dùng chung + chỉ số mục kế tiếp ở mỗi tầng
 * (stack UI task không phình theo độ sâu). Mỗi bước làm một đơn vị việc nhỏ (một mục, hoặc một khối dữ liệu).
 */
#include "framework/file_service.hpp"

#include <cstdio>
#include <cstring>

#include "common/log.hpp"
#include "common/string_utils.hpp"
#include "hal_api/board.hpp"
#include "osal/osal.hpp"

namespace fw {

namespace {

constexpr const char* TAG = "files";
constexpr size_t COPY_CHUNK = 4096U;
constexpr int32_t NO_FILE = -1;

uint8_t s_copy_buf[COPY_CHUNK]; // Chỉ UI task dùng (job chạy từng bước trên UI task).

hal::IFileSystem* fs_of(const hal::Devices* p_devices)
{
    if ((p_devices == nullptr) || (p_devices->p_fs == nullptr) || !p_devices->p_fs->is_mounted()) {
        return nullptr;
    }
    return p_devices->p_fs;
}

void to_entry(const hal::DirEntry& in, FileEntry& out)
{
    (void)common::copy_string(out.name, sizeof(out.name), in.name);
    out.is_dir = in.is_dir;
    out.size = in.size;
    out.mtime = in.mtime;
}

struct ListCtx {
    FileVisitor visitor;
    void* p_ctx;
};

bool forward_entry(const hal::DirEntry& entry, void* p_ctx)
{
    auto* const p_list = static_cast<ListCtx*>(p_ctx);
    FileEntry out{};
    to_entry(entry, out);
    return p_list->visitor(out, p_list->p_ctx);
}

struct NthCtx {
    uint16_t target;
    uint16_t seen;
    bool found;
    FileEntry* p_out;
};

bool pick_nth(const hal::DirEntry& entry, void* p_ctx)
{
    auto* const p_nth = static_cast<NthCtx*>(p_ctx);
    if (p_nth->seen == p_nth->target) {
        to_entry(entry, *p_nth->p_out);
        p_nth->found = true;
        return false;
    }
    ++p_nth->seen;
    return true;
}

bool is_inside(const char* p_child, const char* p_parent)
{
    const size_t len = std::strlen(p_parent);
    return (std::strncmp(p_child, p_parent, len) == 0) && ((p_child[len] == '/') || (p_child[len] == '\0'));
}

void strip_last(char* p_path)
{
    char* const p_slash = std::strrchr(p_path, '/');
    if (p_slash != nullptr) {
        *p_slash = '\0';
    } else {
        p_path[0] = '\0';
    }
}

} // namespace

FileService::FileService() : m_p_devices(nullptr), m_job{}
{
    m_job.src_file = NO_FILE;
    m_job.dst_file = NO_FILE;
}

void FileService::bind(const hal::Devices& devices)
{
    m_p_devices = &devices;
}

bool FileService::is_available() const
{
    return fs_of(m_p_devices) != nullptr;
}

bool FileService::capacity(uint64_t& total_bytes, uint64_t& free_bytes) const
{
    hal::IFileSystem* const p_fs = fs_of(m_p_devices);
    return (p_fs != nullptr) && common::is_ok(p_fs->get_capacity(total_bytes, free_bytes));
}

common::Status FileService::list(const char* p_dir, FileVisitor visitor, void* p_ctx) const
{
    hal::IFileSystem* const p_fs = fs_of(m_p_devices);
    if ((p_fs == nullptr) || (visitor == nullptr) || (p_dir == nullptr)) {
        return common::Status::ERR_STATE;
    }
    ListCtx ctx{visitor, p_ctx};
    return p_fs->list_dir_detailed(p_dir, &forward_entry, &ctx);
}

common::Status FileService::stat(const char* p_path, FileEntry& entry) const
{
    entry = FileEntry{};
    hal::IFileSystem* const p_fs = fs_of(m_p_devices);
    if ((p_fs == nullptr) || (p_path == nullptr)) {
        return common::Status::ERR_STATE;
    }
    hal::DirEntry raw{};
    const common::Status status = p_fs->stat(p_path, raw);
    if (common::is_ok(status)) {
        to_entry(raw, entry);
        return status;
    }
    if (status != common::Status::ERR_NOT_SUPPORTED) {
        return status;
    }
    bool is_dir = false; // Platform chưa có stat(): vẫn biết tồn tại + loại.
    if (!p_fs->exists(p_path, is_dir)) {
        return common::Status::ERR_NOT_FOUND;
    }
    (void)common::copy_string(entry.name, sizeof(entry.name), base_name(p_path));
    entry.is_dir = is_dir;
    return common::Status::OK;
}

bool FileService::exists(const char* p_path) const
{
    hal::IFileSystem* const p_fs = fs_of(m_p_devices);
    bool is_dir = false;
    return (p_fs != nullptr) && (p_path != nullptr) && p_fs->exists(p_path, is_dir);
}

common::Status FileService::make_dir(const char* p_path)
{
    hal::IFileSystem* const p_fs = fs_of(m_p_devices);
    if (p_fs == nullptr) {
        return common::Status::ERR_STATE;
    }
    if (exists(p_path)) {
        return common::Status::ERR_STATE;
    }
    const common::Status status = p_fs->make_dir(p_path);
    if (common::is_ok(status)) {
        LOGI(TAG, "mkdir %s", p_path);
    }
    return status;
}

common::Status FileService::rename(const char* p_from, const char* p_to)
{
    hal::IFileSystem* const p_fs = fs_of(m_p_devices);
    if ((p_fs == nullptr) || (p_from == nullptr) || (p_to == nullptr)) {
        return common::Status::ERR_STATE;
    }
    if (is_inside(p_to, p_from)) {
        return common::Status::ERR_PARAM; // Không chuyển thư mục vào chính nó.
    }
    const common::Status status = p_fs->rename(p_from, p_to);
    if (common::is_ok(status)) {
        LOGI(TAG, "rename %s -> %s", p_from, p_to);
    } else {
        LOGW(TAG, "rename %s -> %s failed (status=%u)", p_from, p_to, static_cast<unsigned>(status));
    }
    return status;
}

common::Status FileService::read_file(const char* p_path, uint8_t* p_buf, size_t cap, size_t& len) const
{
    len = 0U;
    hal::IFileSystem* const p_fs = fs_of(m_p_devices);
    if ((p_fs == nullptr) || (p_buf == nullptr)) {
        return common::Status::ERR_STATE;
    }
    hal::FileHandle handle = hal::INVALID_FILE;
    common::Status status = p_fs->open(p_path, hal::OpenMode::READ, handle);
    if (!common::is_ok(status)) {
        return status;
    }
    while (len < cap) {
        size_t got = 0U;
        status = p_fs->read(handle, &p_buf[len], cap - len, got);
        if (!common::is_ok(status) || (got == 0U)) {
            break;
        }
        len += got;
    }
    p_fs->close(handle);
    return status;
}

common::Status FileService::write_file(const char* p_path, const uint8_t* p_data, size_t len)
{
    hal::IFileSystem* const p_fs = fs_of(m_p_devices);
    if (p_fs == nullptr) {
        return common::Status::ERR_STATE;
    }
    hal::FileHandle handle = hal::INVALID_FILE;
    common::Status status = p_fs->open(p_path, hal::OpenMode::WRITE, handle);
    if (!common::is_ok(status)) {
        return status;
    }
    if (len > 0U) {
        status = p_fs->write(handle, p_data, len);
    }
    p_fs->close(handle);
    return status;
}

// ============================================================================
// Job chép / xóa
// ============================================================================
common::Status FileService::start_copy(const char* p_from, const char* p_to)
{
    hal::IFileSystem* const p_fs = fs_of(m_p_devices);
    if ((p_fs == nullptr) || (m_job.state == FileJobState::RUNNING)) {
        return common::Status::ERR_STATE;
    }
    FileEntry source{};
    common::Status status = stat(p_from, source);
    if (!common::is_ok(status)) {
        return status;
    }
    if (exists(p_to)) {
        return common::Status::ERR_STATE;
    }
    if (source.is_dir && is_inside(p_to, p_from)) {
        return common::Status::ERR_PARAM;
    }
    m_job = Job{};
    m_job.kind = JobKind::COPY;
    m_job.src_file = NO_FILE;
    m_job.dst_file = NO_FILE;
    (void)common::copy_string(m_job.src, sizeof(m_job.src), p_from);
    (void)common::copy_string(m_job.dst, sizeof(m_job.dst), p_to);
    if (source.is_dir) {
        status = p_fs->make_dir(p_to);
        m_job.phase = Phase::SCAN;
    } else {
        status = p_fs->open(p_from, hal::OpenMode::READ, m_job.src_file);
        if (common::is_ok(status)) {
            status = p_fs->open(p_to, hal::OpenMode::WRITE, m_job.dst_file);
        }
        m_job.phase = Phase::COPY_FILE;
        m_job.depth = 0U;
        m_job.next_index[0] = UINT16_MAX; // Đánh dấu: gốc là một file (chép xong là DONE).
    }
    if (!common::is_ok(status)) {
        close_job_files();
        m_job.state = FileJobState::IDLE;
        return status;
    }
    m_job.state = FileJobState::RUNNING;
    LOGI(TAG, "copy %s -> %s", p_from, p_to);
    return common::Status::OK;
}

common::Status FileService::start_delete(const char* p_path)
{
    hal::IFileSystem* const p_fs = fs_of(m_p_devices);
    if ((p_fs == nullptr) || (m_job.state == FileJobState::RUNNING) || (p_path == nullptr) || (p_path[0] == '\0')) {
        return common::Status::ERR_STATE;
    }
    FileEntry target{};
    const common::Status status = stat(p_path, target);
    if (!common::is_ok(status)) {
        return status;
    }
    m_job = Job{};
    m_job.kind = JobKind::DELETE;
    m_job.phase = Phase::SCAN;
    m_job.src_file = NO_FILE;
    m_job.dst_file = NO_FILE;
    (void)common::copy_string(m_job.src, sizeof(m_job.src), p_path);
    LOGI(TAG, "delete %s", p_path);
    if (!target.is_dir) {
        const common::Status removed = p_fs->remove(p_path);
        m_job.items = 1U;
        m_job.state = common::is_ok(removed) ? FileJobState::DONE : FileJobState::FAILED;
        m_job.error = removed;
        return common::Status::OK;
    }
    m_job.state = FileJobState::RUNNING;
    return common::Status::OK;
}

FileJobState FileService::step(uint32_t budget_ms)
{
    const uint32_t start_ms = osal::tick_ms();
    while (m_job.state == FileJobState::RUNNING) {
        const bool more = (m_job.kind == JobKind::COPY) ? step_copy() : step_delete();
        if (!more && (m_job.state == FileJobState::RUNNING)) {
            m_job.state = FileJobState::DONE;
        }
        if ((osal::tick_ms() - start_ms) >= budget_ms) {
            break;
        }
    }
    if (m_job.state == FileJobState::DONE) {
        LOGI(TAG, "job done: %u item(s), %u byte(s)", static_cast<unsigned>(m_job.items),
             static_cast<unsigned>(m_job.bytes));
    }
    return m_job.state;
}

void FileService::cancel_job()
{
    if (m_job.state == FileJobState::RUNNING) {
        close_job_files();
        m_job.state = FileJobState::IDLE;
        LOGI(TAG, "job cancelled after %u item(s)", static_cast<unsigned>(m_job.items));
    }
}

bool FileService::child_at(const char* p_dir, uint16_t index, FileEntry& out) const
{
    hal::IFileSystem* const p_fs = fs_of(m_p_devices);
    NthCtx ctx{index, 0U, false, &out};
    return (p_fs != nullptr) && common::is_ok(p_fs->list_dir(p_dir, &pick_nth, &ctx)) && ctx.found;
}

bool FileService::push_child(const char* p_name)
{
    char next[FILE_PATH_BUF_LEN];
    if (!join(next, sizeof(next), m_job.src, p_name)) {
        return false;
    }
    (void)common::copy_string(m_job.src, sizeof(m_job.src), next);
    if (m_job.kind == JobKind::COPY) {
        if (!join(next, sizeof(next), m_job.dst, p_name)) {
            return false;
        }
        (void)common::copy_string(m_job.dst, sizeof(m_job.dst), next);
    }
    return true;
}

void FileService::pop_child()
{
    strip_last(m_job.src);
    if (m_job.kind == JobKind::COPY) {
        strip_last(m_job.dst);
    }
}

void FileService::close_job_files()
{
    hal::IFileSystem* const p_fs = fs_of(m_p_devices);
    if (p_fs != nullptr) {
        if (m_job.src_file != NO_FILE) {
            p_fs->close(m_job.src_file);
        }
        if (m_job.dst_file != NO_FILE) {
            p_fs->close(m_job.dst_file);
        }
    }
    m_job.src_file = NO_FILE;
    m_job.dst_file = NO_FILE;
}

void FileService::fail(common::Status status)
{
    close_job_files();
    m_job.error = status;
    m_job.state = FileJobState::FAILED;
    LOGW(TAG, "job failed at %s (status=%u)", m_job.src, static_cast<unsigned>(status));
}

bool FileService::step_copy()
{
    hal::IFileSystem* const p_fs = fs_of(m_p_devices);
    if (p_fs == nullptr) {
        fail(common::Status::ERR_STATE);
        return false;
    }
    if (m_job.phase == Phase::COPY_FILE) {
        size_t got = 0U;
        common::Status status = p_fs->read(m_job.src_file, s_copy_buf, sizeof(s_copy_buf), got);
        if (common::is_ok(status) && (got > 0U)) {
            status = p_fs->write(m_job.dst_file, s_copy_buf, got);
            m_job.bytes += static_cast<uint32_t>(got);
        }
        if (!common::is_ok(status)) {
            fail(status);
            return false;
        }
        if (got > 0U) {
            return true;
        }
        close_job_files(); // Hết file.
        ++m_job.items;
        if (m_job.next_index[0] == UINT16_MAX) {
            return false; // Gốc là một file: xong.
        }
        pop_child();
        m_job.phase = Phase::SCAN;
        return true;
    }

    FileEntry child{};
    if (!child_at(m_job.src, m_job.next_index[m_job.depth], child)) {
        ++m_job.items; // Xong một thư mục.
        if (m_job.depth == 0U) {
            return false;
        }
        pop_child();
        --m_job.depth;
        return true;
    }
    ++m_job.next_index[m_job.depth];
    if (!push_child(child.name)) {
        fail(common::Status::ERR_PARAM);
        return false;
    }
    if (child.is_dir) {
        if (m_job.depth >= MAX_DEPTH) {
            fail(common::Status::ERR_NOT_SUPPORTED);
            return false;
        }
        const common::Status status = p_fs->make_dir(m_job.dst);
        if (!common::is_ok(status)) {
            fail(status);
            return false;
        }
        ++m_job.depth;
        m_job.next_index[m_job.depth] = 0U;
        return true;
    }
    common::Status status = p_fs->open(m_job.src, hal::OpenMode::READ, m_job.src_file);
    if (common::is_ok(status)) {
        status = p_fs->open(m_job.dst, hal::OpenMode::WRITE, m_job.dst_file);
    }
    if (!common::is_ok(status)) {
        fail(status);
        return false;
    }
    m_job.phase = Phase::COPY_FILE;
    return true;
}

bool FileService::step_delete()
{
    hal::IFileSystem* const p_fs = fs_of(m_p_devices);
    if (p_fs == nullptr) {
        fail(common::Status::ERR_STATE);
        return false;
    }
    FileEntry child{};
    if (!child_at(m_job.src, 0U, child)) {
        const common::Status status = p_fs->remove(m_job.src); // Thư mục đã rỗng.
        if (!common::is_ok(status)) {
            fail(status);
            return false;
        }
        ++m_job.items;
        if (m_job.depth == 0U) {
            return false;
        }
        pop_child();
        --m_job.depth;
        return true;
    }
    if (!push_child(child.name)) {
        fail(common::Status::ERR_PARAM);
        return false;
    }
    if (child.is_dir) {
        if (m_job.depth >= MAX_DEPTH) {
            fail(common::Status::ERR_NOT_SUPPORTED);
            return false;
        }
        ++m_job.depth;
        return true;
    }
    const common::Status status = p_fs->remove(m_job.src);
    if (!common::is_ok(status)) {
        fail(status);
        return false;
    }
    ++m_job.items;
    pop_child();
    return true;
}

// ============================================================================
// Tiện ích đường dẫn
// ============================================================================
bool FileService::join(char* p_out, size_t out_len, const char* p_dir, const char* p_name)
{
    const bool is_root = (p_dir == nullptr) || (p_dir[0] == '\0');
    const int written = is_root ? std::snprintf(p_out, out_len, "%s", p_name)
                                : std::snprintf(p_out, out_len, "%s/%s", p_dir, p_name);
    return (written > 0) && (static_cast<size_t>(written) < out_len) &&
           (static_cast<size_t>(written) <= FILE_PATH_MAX_LEN);
}

bool FileService::is_valid_name(const char* p_name)
{
    if ((p_name == nullptr) || (p_name[0] == '\0') || (std::strcmp(p_name, ".") == 0) ||
        (std::strcmp(p_name, "..") == 0)) {
        return false;
    }
    size_t len = 0U;
    for (; p_name[len] != '\0'; ++len) {
        const auto c = static_cast<unsigned char>(p_name[len]);
        constexpr unsigned char FIRST_PRINTABLE = 0x20U;
        if ((c < FIRST_PRINTABLE) || (c == '/') || (c == '\\') || (c == ':') || (c == '*') || (c == '?') ||
            (c == '"') || (c == '<') || (c == '>') || (c == '|')) {
            return false; // Ký tự FAT không cho phép.
        }
    }
    return len <= FILE_NAME_MAX_LEN;
}

const char* FileService::base_name(const char* p_path)
{
    if (p_path == nullptr) {
        return "";
    }
    const char* const p_slash = std::strrchr(p_path, '/');
    return (p_slash != nullptr) ? (p_slash + 1) : p_path;
}

} // namespace fw
