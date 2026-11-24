/**
 * @file    bridge_fs.cpp
 * @brief   Lệnh filesystem kiểu Linux của debug bridge: cd, pwd, ls, mkdir, rm, rmdir, cp, mv, cat, head, tail,
 *          stat, du, df, find, grep, touch, tree, mount, format.
 *
 * - Thư mục hiện tại giữ theo phiên `gach` (reset về "/" khi phiên mới bắt đầu). Đường dẫn: tuyệt đối ("/" = gốc
 *   thẻ), tương đối, ".", "..", "~".
 * - Duyệt cây không đệ quy: mỗi tầng một bộ đệm mục tĩnh (runtime .bss nằm trong PSRAM), sâu tối đa MAX_DEPTH.
 * - Thao tác file giữ fw::UiLock trong lúc gọi HAL (UI / app cũng dùng thẻ); không giữ khóa khi gửi dữ liệu ra cổng.
 * - cp -r / rm -r dùng job từng bước của fw::FileService.
 */
#include <cstdarg>
#include <cstdio>
#include <cstdlib>
#include <cstring>

#include "bridge_sys.hpp"
#include "common/string_utils.hpp"
#include "framework/file_service.hpp"
#include "framework/services.hpp"
#include "framework/system.hpp"
#include "framework/ui_lock.hpp"
#include "osal/osal.hpp"

namespace runtime {

namespace {

constexpr size_t LINE_LEN = 200U;
constexpr size_t PATH_LEN = hal::FS_PATH_MAX_LEN + 1U;
constexpr uint8_t MAX_DEPTH = 8U;
constexpr size_t LEVEL_ENTRIES = 96U;   /**< Mục tối đa mỗi thư mục khi duyệt cây. */
constexpr size_t LS_ENTRIES = 256U;
constexpr size_t READ_CHUNK = 512U;
constexpr size_t CAT_MAX_LINES = 2000U;
constexpr size_t HEAD_DEFAULT = 10U;
constexpr size_t TAIL_MAX = 100U;
constexpr size_t FIND_MAX = 500U;
constexpr size_t GREP_MAX = 300U;
constexpr size_t TREE_MAX = 500U;
constexpr uint32_t JOB_STEP_MS = 20U;
constexpr uint32_t KIB = 1024U;
constexpr uint32_t MIB = KIB * KIB;
constexpr uint32_t SECONDS_PER_DAY = 86400U;
constexpr uint32_t MIN_VALID_EPOCH = 1577836800U; /**< 2020: FAT chưa có giờ thật thì in "-". */
constexpr uint32_t REBOOT_DELAY_MS = 300U;
constexpr const char* MOUNT_ROOT_DIRS[] = {"apps", "install"};

const hal::Devices* s_p_dev = nullptr;
char s_cwd[PATH_LEN] = {}; // Tương đối gốc thẻ, "" = "/".
hal::DirEntry s_ls[LS_ENTRIES];
hal::DirEntry s_levels[MAX_DEPTH + 1U][LEVEL_ENTRIES];
size_t s_level_count[MAX_DEPTH + 1U];
char s_tail[TAIL_MAX][LINE_LEN];
fw::FileService s_files; // Job cp -r / rm -r (bước nào cũng giữ UiLock).

void line(uint8_t id, const char* p_fmt, ...) __attribute__((format(printf, 2, 3)));
void line(uint8_t id, const char* p_fmt, ...)
{
    char text[LINE_LEN];
    va_list args;
    va_start(args, p_fmt);
    (void)std::vsnprintf(text, sizeof(text), p_fmt, args);
    va_end(args);
    bridge_text(id, text);
}

hal::IFileSystem* fs()
{
    return ((s_p_dev != nullptr) && (s_p_dev->p_fs != nullptr) && s_p_dev->p_fs->is_mounted()) ? s_p_dev->p_fs
                                                                                                : nullptr;
}

// ============================================================================
// Đường dẫn
// ============================================================================
/** @brief Chuẩn hóa: gộp "/", bỏ ".", xử lý ".."; kết quả tương đối gốc thẻ ("" = gốc). */
bool normalize(const char* p_in, char* p_out, size_t out_len)
{
    char work[PATH_LEN * 2U];
    const bool is_abs = (p_in[0] == '/') || (p_in[0] == '~');
    const char* const p_rest = (p_in[0] == '~') ? (p_in + 1) : p_in;
    if (is_abs || (s_cwd[0] == '\0')) {
        (void)std::snprintf(work, sizeof(work), "%s", p_rest);
    } else {
        (void)std::snprintf(work, sizeof(work), "%s/%s", s_cwd, p_rest);
    }
    size_t used = 0U;
    p_out[0] = '\0';
    char* p_save = nullptr;
    for (char* p_part = strtok_r(work, "/", &p_save); p_part != nullptr; p_part = strtok_r(nullptr, "/", &p_save)) {
        if (std::strcmp(p_part, ".") == 0) {
            continue;
        }
        if (std::strcmp(p_part, "..") == 0) {
            char* const p_slash = std::strrchr(p_out, '/');
            used = (p_slash != nullptr) ? static_cast<size_t>(p_slash - p_out) : 0U;
            p_out[used] = '\0';
            continue;
        }
        const int written = std::snprintf(&p_out[used], out_len - used, "%s%s", (used > 0U) ? "/" : "", p_part);
        if ((written < 0) || (static_cast<size_t>(written) >= (out_len - used))) {
            return false;
        }
        used += static_cast<size_t>(written);
    }
    return used <= hal::FS_PATH_MAX_LEN;
}

/** @brief "/" + đường dẫn tương đối (để in). */
const char* shown(const char* p_rel, char* p_buf, size_t len)
{
    (void)std::snprintf(p_buf, len, "/%s", p_rel);
    return p_buf;
}

const char* base_name(const char* p_path)
{
    const char* const p_slash = std::strrchr(p_path, '/');
    return (p_slash != nullptr) ? (p_slash + 1) : p_path;
}

bool join(char* p_out, size_t len, const char* p_dir, const char* p_name)
{
    const int written = (p_dir[0] == '\0') ? std::snprintf(p_out, len, "%s", p_name)
                                           : std::snprintf(p_out, len, "%s/%s", p_dir, p_name);
    return (written > 0) && (static_cast<size_t>(written) < len);
}

/** @return true nếu tồn tại; is_dir / entry được điền. Gốc thẻ ("") là thư mục. */
bool stat_path(const char* p_rel, hal::DirEntry& entry)
{
    entry = hal::DirEntry{};
    if (p_rel[0] == '\0') {
        entry.is_dir = true;
        return true;
    }
    fw::UiLock lock;
    if (common::is_ok(fs()->stat(p_rel, entry))) {
        return true;
    }
    bool is_dir = false; // Platform không có stat(): vẫn biết tồn tại + loại.
    if (!fs()->exists(p_rel, is_dir)) {
        return false;
    }
    (void)common::copy_string(entry.name, sizeof(entry.name), base_name(p_rel));
    entry.is_dir = is_dir;
    return true;
}

void fmt_size(uint64_t bytes, char* p_out, size_t len)
{
    if (bytes >= MIB) {
        (void)std::snprintf(p_out, len, "%.1fM", static_cast<double>(bytes) / MIB);
    } else if (bytes >= KIB) {
        (void)std::snprintf(p_out, len, "%.1fK", static_cast<double>(bytes) / KIB);
    } else {
        (void)std::snprintf(p_out, len, "%u", static_cast<unsigned>(bytes));
    }
}

void fmt_time(uint32_t mtime, char* p_out, size_t len)
{
    if (mtime < MIN_VALID_EPOCH) {
        (void)std::snprintf(p_out, len, "%16s", "-");
        return;
    }
    int32_t offset_min = 0;
    {
        fw::UiLock lock;
        offset_min = fw::services().time().utc_offset_minutes();
    }
    const int64_t local = static_cast<int64_t>(mtime) + (static_cast<int64_t>(offset_min) * 60);
    const int64_t days = local / SECONDS_PER_DAY;
    const int64_t secs = local % SECONDS_PER_DAY;
    const int64_t z = days + 719468;
    const int64_t era = z / 146097;
    const int64_t doe = z - (era * 146097);
    const int64_t yoe = (doe - doe / 1460 + doe / 36524 - doe / 146096) / 365;
    const int64_t doy = doe - (365 * yoe + yoe / 4 - yoe / 100);
    const int64_t mp = (5 * doy + 2) / 153;
    const int64_t day = doy - (153 * mp + 2) / 5 + 1;
    const int64_t month = (mp < 10) ? (mp + 3) : (mp - 9);
    const int64_t year = yoe + era * 400 + ((month <= 2) ? 1 : 0);
    (void)std::snprintf(p_out, len, "%04d-%02d-%02d %02d:%02d", static_cast<int>(year), static_cast<int>(month),
                        static_cast<int>(day), static_cast<int>(secs / 3600), static_cast<int>((secs / 60) % 60));
}

/** @brief Khớp mẫu kiểu shell: * và ? (không phân biệt hoa thường). */
bool glob_match(const char* p_pat, const char* p_name)
{
    const auto lower = [](char c) { return ((c >= 'A') && (c <= 'Z')) ? static_cast<char>(c - 'A' + 'a') : c; };
    const char* p_star = nullptr;
    const char* p_retry = nullptr;
    while (*p_name != '\0') {
        if ((*p_pat == '?') || ((*p_pat != '*') && (*p_pat != '\0') && (lower(*p_pat) == lower(*p_name)))) {
            ++p_pat;
            ++p_name;
        } else if (*p_pat == '*') {
            p_star = p_pat++;
            p_retry = p_name;
        } else if (p_star != nullptr) {
            p_pat = p_star + 1;
            p_name = ++p_retry;
        } else {
            return false;
        }
    }
    while (*p_pat == '*') {
        ++p_pat;
    }
    return *p_pat == '\0';
}

// ============================================================================
// Duyệt thư mục / cây
// ============================================================================
struct Collect {
    hal::DirEntry* p_out;
    size_t count;
    size_t capacity;
    bool truncated;
};

bool collect(const hal::DirEntry& entry, void* p_ctx)
{
    auto* const p_c = static_cast<Collect*>(p_ctx);
    if (p_c->count >= p_c->capacity) {
        p_c->truncated = true;
        return false;
    }
    p_c->p_out[p_c->count++] = entry;
    return true;
}

/** @brief Đọc cả thư mục vào mảng (dir trước, tên A-Z). @return số mục, -1 nếu lỗi. */
int read_dir(const char* p_rel, hal::DirEntry* p_out, size_t capacity, bool& truncated)
{
    Collect ctx{p_out, 0U, capacity, false};
    common::Status status = common::Status::OK;
    {
        fw::UiLock lock;
        status = fs()->list_dir_detailed(p_rel, &collect, &ctx);
    }
    if (!common::is_ok(status)) {
        return -1;
    }
    truncated = ctx.truncated;
    for (size_t index = 1U; index < ctx.count; ++index) { // Chèn: thư mục trước, tên tăng dần.
        const hal::DirEntry key = p_out[index];
        size_t pos = index;
        while (pos > 0U) {
            const hal::DirEntry& prev = p_out[pos - 1U];
            const bool before = (key.is_dir != prev.is_dir) ? key.is_dir : (std::strcmp(key.name, prev.name) < 0);
            if (!before) {
                break;
            }
            p_out[pos] = prev;
            --pos;
        }
        p_out[pos] = key;
    }
    return static_cast<int>(ctx.count);
}

/**
 * @brief Duyệt cây (thứ tự trước). visit(path, entry, depth, is_last, ctx) trả false để dừng hẳn.
 * @return false nếu dừng sớm.
 */
using Visitor = bool (*)(const char* p_path, const hal::DirEntry& entry, uint8_t depth, bool is_last, void* p_ctx);

bool walk(const char* p_root, uint8_t max_depth, bool show_hidden, Visitor visit, void* p_ctx)
{
    char path[PATH_LEN];
    (void)common::copy_string(path, sizeof(path), p_root);
    size_t next[MAX_DEPTH + 1U] = {};
    size_t path_len[MAX_DEPTH + 1U] = {};
    bool truncated = false;
    int count = read_dir(path, s_levels[0], LEVEL_ENTRIES, truncated);
    s_level_count[0] = (count > 0) ? static_cast<size_t>(count) : 0U;
    path_len[0] = std::strlen(path);
    uint8_t depth = 0U;
    for (;;) {
        if (next[depth] >= s_level_count[depth]) {
            if (depth == 0U) {
                return true;
            }
            --depth;
            path[path_len[depth]] = '\0';
            continue;
        }
        const hal::DirEntry& entry = s_levels[depth][next[depth]++];
        if (!show_hidden && (entry.name[0] == '.')) {
            continue; // Mục ẩn: không thăm, không đi vào.
        }
        bool is_last = true; // Mục hiển thị cuối cùng của thư mục (bỏ qua mục ẩn phía sau).
        for (size_t later = next[depth]; later < s_level_count[depth]; ++later) {
            if (show_hidden || (s_levels[depth][later].name[0] != '.')) {
                is_last = false;
                break;
            }
        }
        char child[PATH_LEN];
        if (!join(child, sizeof(child), path, entry.name)) {
            continue;
        }
        if (!visit(child, entry, depth, is_last, p_ctx)) {
            return false;
        }
        if (entry.is_dir && ((depth + 1U) < max_depth) && ((depth + 1U) <= MAX_DEPTH)) {
            ++depth;
            (void)common::copy_string(path, sizeof(path), child);
            path_len[depth] = std::strlen(path);
            next[depth] = 0U;
            count = read_dir(path, s_levels[depth], LEVEL_ENTRIES, truncated);
            s_level_count[depth] = (count > 0) ? static_cast<size_t>(count) : 0U;
        }
    }
}

// ============================================================================
// Đọc file theo dòng
// ============================================================================
using LineVisitor = bool (*)(const char* p_line, size_t number, void* p_ctx);

/** @brief Đọc file, gọi visit cho từng dòng (dòng dài hơn LINE_LEN bị chia). @return false nếu không mở được. */
bool read_lines(const char* p_rel, LineVisitor visit, void* p_ctx, bool& is_binary)
{
    is_binary = false;
    hal::FileHandle handle = hal::INVALID_FILE;
    {
        fw::UiLock lock;
        if (!common::is_ok(fs()->open(p_rel, hal::OpenMode::READ, handle))) {
            return false;
        }
    }
    uint8_t chunk[READ_CHUNK];
    char text[LINE_LEN];
    size_t used = 0U;
    size_t number = 1U;
    bool keep_going = true;
    bool first_chunk = true;
    while (keep_going) {
        size_t got = 0U;
        const common::Status status = fs()->read(handle, chunk, sizeof(chunk), got); // File riêng: không cần khóa.
        if (!common::is_ok(status) || (got == 0U)) {
            break;
        }
        if (first_chunk && (std::memchr(chunk, 0, got) != nullptr)) {
            is_binary = true;
            break;
        }
        first_chunk = false;
        for (size_t index = 0U; (index < got) && keep_going; ++index) {
            const char c = static_cast<char>(chunk[index]);
            if ((c == '\n') || (used == (sizeof(text) - 1U))) {
                text[used] = '\0';
                keep_going = visit(text, number, p_ctx);
                number += (c == '\n') ? 1U : 0U;
                used = 0U;
                if (c == '\n') {
                    continue;
                }
            }
            if (c != '\r') {
                text[used++] = (c == '\t') ? ' ' : c;
            }
        }
    }
    if (keep_going && (used > 0U) && !is_binary) {
        text[used] = '\0';
        (void)visit(text, number, p_ctx);
    }
    {
        fw::UiLock lock;
        fs()->close(handle);
    }
    return true;
}

// ============================================================================
// Lệnh
// ============================================================================
struct Args {
    const char* p_positional[3];
    size_t positional;
    bool flags[26]; /**< -a..-z */
    long number;    /**< Giá trị sau -n / -L. */
    const char* p_name; /**< Giá trị sau -name. */
    char type;      /**< Sau -type: 'f' / 'd' / 0. */
};

Args parse(size_t argc, char* const p_args[], size_t first)
{
    Args a{};
    a.number = -1;
    for (size_t index = first; index < argc; ++index) {
        const char* const p_arg = p_args[index];
        const bool next_is_number = ((index + 1U) < argc) && (p_args[index + 1U][0] >= '0') &&
                                    (p_args[index + 1U][0] <= '9');
        if (((std::strcmp(p_arg, "-n") == 0) || (std::strcmp(p_arg, "-L") == 0)) && next_is_number) {
            a.number = std::strtol(p_args[++index], nullptr, 10); // "-n 20"; "cat -n file" = cờ đánh số dòng.
        } else if (std::strcmp(p_arg, "-name") == 0) {
            a.p_name = ((index + 1U) < argc) ? p_args[++index] : nullptr;
        } else if (std::strcmp(p_arg, "-type") == 0) {
            a.type = ((index + 1U) < argc) ? p_args[++index][0] : '\0';
        } else if ((p_arg[0] == '-') && (p_arg[1] != '\0') && (std::strcmp(p_arg, "--yes") != 0)) {
            for (const char* p = p_arg + 1; *p != '\0'; ++p) {
                if ((*p >= 'a') && (*p <= 'z')) {
                    a.flags[*p - 'a'] = true;
                }
            }
        } else if (a.positional < 3U) {
            a.p_positional[a.positional++] = p_arg;
        } else {
            // Thừa tham số: bỏ qua.
        }
    }
    return a;
}

bool flag(const Args& a, char c)
{
    return a.flags[c - 'a'];
}

/** @return đường dẫn đã chuẩn hóa của tham số vị trí index (mặc định cwd); false + đã báo lỗi nếu quá dài. */
bool arg_path(uint8_t id, const Args& a, size_t index, char* p_out)
{
    const char* const p_in = (index < a.positional) ? a.p_positional[index] : ".";
    if (!normalize(p_in, p_out, PATH_LEN)) {
        bridge_resp(id, false, "path too long");
        return false;
    }
    return true;
}

void cmd_pwd(uint8_t id)
{
    char buf[PATH_LEN + 1U];
    bridge_resp(id, true, shown(s_cwd, buf, sizeof(buf)));
}

void cmd_cd(uint8_t id, const Args& a)
{
    char target[PATH_LEN];
    if (a.positional == 0U) {
        target[0] = '\0';
    } else if (!arg_path(id, a, 0U, target)) {
        return;
    }
    hal::DirEntry entry{};
    if (!stat_path(target, entry) || !entry.is_dir) {
        bridge_resp(id, false, "no such directory");
        return;
    }
    (void)common::copy_string(s_cwd, sizeof(s_cwd), target);
    char buf[PATH_LEN + 1U];
    bridge_resp(id, true, shown(s_cwd, buf, sizeof(buf)));
}

void cmd_ls(uint8_t id, const Args& a)
{
    char target[PATH_LEN];
    if (!arg_path(id, a, 0U, target)) {
        return;
    }
    hal::DirEntry entry{};
    if (!stat_path(target, entry)) {
        bridge_resp(id, false, "no such file or directory");
        return;
    }
    const bool is_long = flag(a, 'l');
    const bool show_all = flag(a, 'a');
    size_t count = 1U;
    bool truncated = false;
    if (entry.is_dir) {
        const int listed = read_dir(target, s_ls, LS_ENTRIES, truncated);
        if (listed < 0) {
            bridge_resp(id, false, "cannot list directory");
            return;
        }
        count = static_cast<size_t>(listed);
    } else {
        s_ls[0] = entry;
    }
    size_t shown_count = 0U;
    uint64_t total = 0U;
    for (size_t index = 0U; index < count; ++index) {
        const hal::DirEntry& e = s_ls[index];
        if (!show_all && (e.name[0] == '.')) {
            continue;
        }
        ++shown_count;
        total += e.size;
        if (is_long) {
            char size[16];
            char time[32];
            fmt_size(e.size, size, sizeof(size));
            fmt_time(e.mtime, time, sizeof(time));
            line(id, "%c %8s  %s  %s%s", e.is_dir ? 'd' : '-', e.is_dir ? "-" : size, time, e.name, e.is_dir ? "/" : "");
        } else {
            line(id, "%s%s", e.name, e.is_dir ? "/" : "");
        }
    }
    char summary[64];
    char size[16];
    fmt_size(total, size, sizeof(size));
    (void)std::snprintf(summary, sizeof(summary), "%u item(s), %s%s", static_cast<unsigned>(shown_count), size,
                        truncated ? " (list truncated)" : "");
    bridge_resp(id, true, summary);
}

void cmd_mkdir(uint8_t id, const Args& a)
{
    char target[PATH_LEN];
    if ((a.positional == 0U) || !arg_path(id, a, 0U, target)) {
        if (a.positional == 0U) {
            bridge_resp(id, false, "usage: mkdir [-p] <dir>");
        }
        return;
    }
    fw::UiLock lock;
    if (flag(a, 'p')) { // Tạo từng cấp.
        char partial[PATH_LEN];
        for (size_t index = 0U; target[index] != '\0'; ++index) {
            if (target[index + 1U] == '/' || target[index + 1U] == '\0') {
                (void)std::snprintf(partial, sizeof(partial), "%.*s", static_cast<int>(index + 1U), target);
                bool is_dir = false;
                if (!fs()->exists(partial, is_dir)) {
                    (void)fs()->make_dir(partial);
                }
            }
        }
    }
    bool is_dir = false;
    if (!flag(a, 'p') && fs()->exists(target, is_dir)) {
        bridge_resp(id, false, "already exists");
        return;
    }
    const common::Status status = flag(a, 'p') ? common::Status::OK : fs()->make_dir(target);
    bridge_resp(id, common::is_ok(status) && fs()->exists(target, is_dir), common::is_ok(status) ? nullptr : "failed");
}

/** @brief Chạy job của s_files tới khi xong (mỗi bước giữ UiLock). */
bool run_job(uint8_t id, const char* p_verb)
{
    uint32_t last_report = osal::tick_ms();
    for (;;) {
        fw::FileJobState state = fw::FileJobState::IDLE;
        {
            fw::UiLock lock;
            state = s_files.step(JOB_STEP_MS);
        }
        if (state != fw::FileJobState::RUNNING) {
            return state == fw::FileJobState::DONE;
        }
        constexpr uint32_t REPORT_MS = 2000U;
        if ((osal::tick_ms() - last_report) >= REPORT_MS) {
            last_report = osal::tick_ms();
            line(id, "%s... %u item(s), %u KB", p_verb, static_cast<unsigned>(s_files.job_items()),
                 static_cast<unsigned>(s_files.job_bytes() / KIB));
        }
        osal::delay_ms(1U); // Nhường UI.
    }
}

void cmd_rm(uint8_t id, const Args& a, bool dir_only)
{
    if (a.positional == 0U) {
        bridge_resp(id, false, dir_only ? "usage: rmdir <dir>" : "usage: rm [-r] [-f] <path>...");
        return;
    }
    size_t removed = 0U;
    for (size_t index = 0U; index < a.positional; ++index) {
        char target[PATH_LEN];
        if (!arg_path(id, a, index, target)) {
            return;
        }
        hal::DirEntry entry{};
        if ((target[0] == '\0') || !stat_path(target, entry)) {
            if (!flag(a, 'f')) {
                line(id, "%s: %s", a.p_positional[index], (target[0] == '\0') ? "refusing to remove /" : "not found");
            }
            continue;
        }
        if (dir_only && !entry.is_dir) {
            line(id, "%s: not a directory", a.p_positional[index]);
            continue;
        }
        if (entry.is_dir && !dir_only && !flag(a, 'r')) {
            line(id, "%s: is a directory (use rm -r)", a.p_positional[index]);
            continue;
        }
        bool is_ok = false;
        if (entry.is_dir && !dir_only) {
            common::Status status = common::Status::OK;
            {
                fw::UiLock lock;
                status = s_files.start_delete(target);
            }
            is_ok = common::is_ok(status) && run_job(id, "removing");
        } else {
            fw::UiLock lock;
            is_ok = common::is_ok(fs()->remove(target));
        }
        if (is_ok) {
            ++removed;
        } else {
            line(id, "%s: %s", a.p_positional[index], dir_only ? "directory not empty or busy" : "remove failed");
        }
    }
    char summary[48];
    (void)std::snprintf(summary, sizeof(summary), "removed %u", static_cast<unsigned>(removed));
    bridge_resp(id, removed > 0U, summary);
}

/** @brief Đích của cp / mv: thư mục có sẵn -> thêm tên nguồn. */
bool target_of(const char* p_src, const char* p_dst, char* p_out)
{
    hal::DirEntry entry{};
    if (stat_path(p_dst, entry) && entry.is_dir) {
        return join(p_out, PATH_LEN, p_dst, base_name(p_src));
    }
    return common::copy_string(p_out, PATH_LEN, p_dst) == std::strlen(p_dst);
}

void cmd_cp_mv(uint8_t id, const Args& a, bool is_move)
{
    char src[PATH_LEN];
    char dst[PATH_LEN];
    char target[PATH_LEN];
    if ((a.positional < 2U) || !arg_path(id, a, 0U, src) || !arg_path(id, a, 1U, dst)) {
        if (a.positional < 2U) {
            bridge_resp(id, false, is_move ? "usage: mv <src> <dst>" : "usage: cp [-r] <src> <dst>");
        }
        return;
    }
    hal::DirEntry entry{};
    if (!stat_path(src, entry)) {
        bridge_resp(id, false, "source not found");
        return;
    }
    if (!target_of(src, dst, target)) {
        bridge_resp(id, false, "path too long");
        return;
    }
    if (is_move) {
        common::Status status = common::Status::OK;
        {
            fw::UiLock lock;
            status = fs()->rename(src, target);
        }
        bridge_resp(id, common::is_ok(status),
                    common::is_ok(status) ? nullptr
                                          : ((status == common::Status::ERR_STATE) ? "target exists" : "move failed"));
        return;
    }
    if (entry.is_dir && !flag(a, 'r')) {
        bridge_resp(id, false, "source is a directory (use cp -r)");
        return;
    }
    common::Status status = common::Status::OK;
    {
        fw::UiLock lock;
        status = s_files.start_copy(src, target);
    }
    if (!common::is_ok(status)) {
        bridge_resp(id, false, (status == common::Status::ERR_STATE) ? "target exists"
                               : (status == common::Status::ERR_PARAM) ? "cannot copy a folder into itself"
                                                                       : "copy failed");
        return;
    }
    const bool is_ok = run_job(id, "copying");
    char summary[64];
    (void)std::snprintf(summary, sizeof(summary), "%s: %u item(s), %u bytes", is_ok ? "copied" : "copy failed",
                        static_cast<unsigned>(s_files.job_items()), static_cast<unsigned>(s_files.job_bytes()));
    bridge_resp(id, is_ok, summary);
}

struct Printer {
    uint8_t id;
    size_t limit;
    size_t printed;
    bool show_numbers;
};

bool print_line(const char* p_line, size_t number, void* p_ctx)
{
    auto* const p_p = static_cast<Printer*>(p_ctx);
    if (p_p->printed >= p_p->limit) {
        return false;
    }
    if (p_p->show_numbers) {
        line(p_p->id, "%6u  %s", static_cast<unsigned>(number), p_line);
    } else {
        bridge_text(p_p->id, (p_line[0] != '\0') ? p_line : " ");
    }
    ++p_p->printed;
    return true;
}

bool require_file(uint8_t id, const char* p_rel)
{
    hal::DirEntry entry{};
    if (!stat_path(p_rel, entry)) {
        bridge_resp(id, false, "no such file");
        return false;
    }
    if (entry.is_dir) {
        bridge_resp(id, false, "is a directory");
        return false;
    }
    return true;
}

void cmd_cat_head(uint8_t id, const Args& a, bool is_head)
{
    char target[PATH_LEN];
    if ((a.positional == 0U) || !arg_path(id, a, 0U, target)) {
        if (a.positional == 0U) {
            bridge_resp(id, false, is_head ? "usage: head [-n N] <file>" : "usage: cat [-n] <file>");
        }
        return;
    }
    if (!require_file(id, target)) {
        return;
    }
    const size_t limit = is_head ? ((a.number > 0) ? static_cast<size_t>(a.number) : HEAD_DEFAULT) : CAT_MAX_LINES;
    Printer printer{id, limit, 0U, !is_head && flag(a, 'n')};
    bool is_binary = false;
    if (!read_lines(target, &print_line, &printer, is_binary)) {
        bridge_resp(id, false, "cannot open file");
        return;
    }
    if (is_binary) {
        bridge_resp(id, false, "binary file (use pull to download it)");
        return;
    }
    bridge_resp(id, true, (!is_head && (printer.printed >= CAT_MAX_LINES)) ? "stopped after 2000 lines" : nullptr);
}

struct TailRing {
    size_t count;
    size_t keep;
};

bool tail_line(const char* p_line, size_t number, void* p_ctx)
{
    (void)number;
    auto* const p_t = static_cast<TailRing*>(p_ctx);
    (void)common::copy_string(s_tail[p_t->count % p_t->keep], LINE_LEN, p_line);
    ++p_t->count;
    return true;
}

void cmd_tail(uint8_t id, const Args& a)
{
    char target[PATH_LEN];
    if ((a.positional == 0U) || !arg_path(id, a, 0U, target)) {
        if (a.positional == 0U) {
            bridge_resp(id, false, "usage: tail [-n N] <file>");
        }
        return;
    }
    if (!require_file(id, target)) {
        return;
    }
    size_t keep = (a.number > 0) ? static_cast<size_t>(a.number) : HEAD_DEFAULT;
    keep = (keep > TAIL_MAX) ? TAIL_MAX : keep;
    TailRing ring{0U, keep};
    bool is_binary = false;
    if (!read_lines(target, &tail_line, &ring, is_binary) || is_binary) {
        bridge_resp(id, false, is_binary ? "binary file" : "cannot open file");
        return;
    }
    const size_t first = (ring.count > keep) ? (ring.count - keep) : 0U;
    for (size_t index = first; index < ring.count; ++index) {
        const char* const p_text = s_tail[index % keep];
        bridge_text(id, (p_text[0] != '\0') ? p_text : " ");
    }
    bridge_resp(id, true, nullptr);
}

void cmd_stat(uint8_t id, const Args& a)
{
    char target[PATH_LEN];
    if (!arg_path(id, a, 0U, target)) {
        return;
    }
    hal::DirEntry entry{};
    if (!stat_path(target, entry)) {
        bridge_resp(id, false, "no such file or directory");
        return;
    }
    char buf[PATH_LEN + 1U];
    char time[32];
    fmt_time(entry.mtime, time, sizeof(time));
    line(id, "path      %s", shown(target, buf, sizeof(buf)));
    line(id, "type      %s", entry.is_dir ? "directory" : "file");
    if (!entry.is_dir) {
        line(id, "size      %u bytes", static_cast<unsigned>(entry.size));
    }
    line(id, "modified  %s", time);
    bridge_resp(id, true, nullptr);
}

struct DuCtx {
    uint8_t id;
    uint64_t total;
    uint64_t current;   /**< Tổng của mục con cấp 1 đang cộng. */
    char current_name[hal::FS_NAME_MAX_LEN + 1U];
    bool current_dir;
    bool summary_only;
    uint32_t files;
};

void du_flush(DuCtx& du)
{
    if (!du.summary_only && (du.current_name[0] != '\0')) {
        char size[16];
        fmt_size(du.current, size, sizeof(size));
        line(du.id, "%8s  %s%s", size, du.current_name, du.current_dir ? "/" : "");
    }
    du.current = 0U;
    du.current_name[0] = '\0';
}

bool du_visit(const char* p_path, const hal::DirEntry& entry, uint8_t depth, bool is_last, void* p_ctx)
{
    (void)p_path;
    (void)is_last;
    auto* const p_du = static_cast<DuCtx*>(p_ctx);
    if (depth == 0U) {
        du_flush(*p_du);
        (void)common::copy_string(p_du->current_name, sizeof(p_du->current_name), entry.name);
        p_du->current_dir = entry.is_dir;
    }
    p_du->current += entry.size;
    p_du->total += entry.size;
    p_du->files += entry.is_dir ? 0U : 1U;
    return true;
}

void cmd_du(uint8_t id, const Args& a)
{
    char target[PATH_LEN];
    if (!arg_path(id, a, 0U, target)) {
        return;
    }
    hal::DirEntry entry{};
    if (!stat_path(target, entry)) {
        bridge_resp(id, false, "no such file or directory");
        return;
    }
    DuCtx du{id, 0U, 0U, {}, false, flag(a, 's'), 0U};
    if (entry.is_dir) {
        (void)walk(target, MAX_DEPTH, true, &du_visit, &du);
        du_flush(du);
    } else {
        du.total = entry.size;
        du.files = 1U;
    }
    char size[16];
    char buf[PATH_LEN + 1U];
    fmt_size(du.total, size, sizeof(size));
    line(id, "%8s  %s  (total, %u file(s))", size, shown(target, buf, sizeof(buf)), static_cast<unsigned>(du.files));
    bridge_resp(id, true, nullptr);
}

void cmd_df(uint8_t id)
{
    uint64_t total = 0U;
    uint64_t free_bytes = 0U;
    common::Status status = common::Status::OK;
    {
        fw::UiLock lock;
        status = fs()->get_capacity(total, free_bytes);
    }
    if (!common::is_ok(status) || (total == 0U)) {
        bridge_resp(id, false, "capacity not available");
        return;
    }
    char size[16];
    char used[16];
    char avail[16];
    fmt_size(total, size, sizeof(size));
    fmt_size(total - free_bytes, used, sizeof(used));
    fmt_size(free_bytes, avail, sizeof(avail));
    constexpr uint64_t PERCENT = 100U;
    line(id, "%-12s %8s %8s %8s %5s  %s", "Filesystem", "Size", "Used", "Avail", "Use%", "Mounted on");
    line(id, "%-12s %8s %8s %8s %4u%%  %s", "sdcard", size, used, avail,
         static_cast<unsigned>(((total - free_bytes) * PERCENT) / total), "/");
    bridge_resp(id, true, nullptr);
}

struct FindCtx {
    uint8_t id;
    const char* p_pattern;
    char type;
    size_t found;
};

bool find_visit(const char* p_path, const hal::DirEntry& entry, uint8_t depth, bool is_last, void* p_ctx)
{
    (void)depth;
    (void)is_last;
    auto* const p_f = static_cast<FindCtx*>(p_ctx);
    const bool type_ok = (p_f->type == '\0') || ((p_f->type == 'd') == entry.is_dir);
    if (type_ok && ((p_f->p_pattern == nullptr) || glob_match(p_f->p_pattern, entry.name))) {
        line(p_f->id, "/%s%s", p_path, entry.is_dir ? "/" : "");
        if (++p_f->found >= FIND_MAX) {
            return false;
        }
    }
    return true;
}

void cmd_find(uint8_t id, const Args& a)
{
    char target[PATH_LEN];
    if (!arg_path(id, a, 0U, target)) {
        return;
    }
    FindCtx ctx{id, a.p_name, a.type, 0U};
    const bool complete = walk(target, MAX_DEPTH, true, &find_visit, &ctx);
    char summary[48];
    (void)std::snprintf(summary, sizeof(summary), "%u match(es)%s", static_cast<unsigned>(ctx.found),
                        complete ? "" : " (stopped at 500)");
    bridge_resp(id, true, summary);
}

struct GrepCtx {
    uint8_t id;
    const char* p_pattern;
    const char* p_file; /**< Đường dẫn để in. */
    bool ignore_case;
    bool show_numbers;
    bool show_file;
    size_t matches;
};

bool contains(const char* p_text, const char* p_pat, bool ignore_case)
{
    if (!ignore_case) {
        return std::strstr(p_text, p_pat) != nullptr;
    }
    const auto lower = [](char c) { return ((c >= 'A') && (c <= 'Z')) ? static_cast<char>(c - 'A' + 'a') : c; };
    for (const char* p = p_text; *p != '\0'; ++p) {
        size_t index = 0U;
        while ((p_pat[index] != '\0') && (p[index] != '\0') && (lower(p[index]) == lower(p_pat[index]))) {
            ++index;
        }
        if (p_pat[index] == '\0') {
            return true;
        }
    }
    return false;
}

bool grep_line(const char* p_line, size_t number, void* p_ctx)
{
    auto* const p_g = static_cast<GrepCtx*>(p_ctx);
    if (!contains(p_line, p_g->p_pattern, p_g->ignore_case)) {
        return true;
    }
    if (p_g->show_file && p_g->show_numbers) {
        line(p_g->id, "/%s:%u: %s", p_g->p_file, static_cast<unsigned>(number), p_line);
    } else if (p_g->show_file) {
        line(p_g->id, "/%s: %s", p_g->p_file, p_line);
    } else if (p_g->show_numbers) {
        line(p_g->id, "%u: %s", static_cast<unsigned>(number), p_line);
    } else {
        bridge_text(p_g->id, p_line);
    }
    return ++p_g->matches < GREP_MAX;
}

bool grep_visit(const char* p_path, const hal::DirEntry& entry, uint8_t depth, bool is_last, void* p_ctx)
{
    (void)depth;
    (void)is_last;
    auto* const p_g = static_cast<GrepCtx*>(p_ctx);
    if (entry.is_dir) {
        return true;
    }
    p_g->p_file = p_path;
    bool is_binary = false;
    (void)read_lines(p_path, &grep_line, p_g, is_binary);
    return p_g->matches < GREP_MAX;
}

void cmd_grep(uint8_t id, const Args& a)
{
    if (a.positional < 2U) {
        bridge_resp(id, false, "usage: grep [-i] [-n] [-r] <text> <file|dir>");
        return;
    }
    char target[PATH_LEN];
    if (!arg_path(id, a, 1U, target)) {
        return;
    }
    hal::DirEntry entry{};
    if (!stat_path(target, entry)) {
        bridge_resp(id, false, "no such file or directory");
        return;
    }
    GrepCtx ctx{id, a.p_positional[0], target, flag(a, 'i'), flag(a, 'n'), entry.is_dir, 0U};
    if (entry.is_dir) {
        if (!flag(a, 'r')) {
            bridge_resp(id, false, "is a directory (use grep -r)");
            return;
        }
        (void)walk(target, MAX_DEPTH, false, &grep_visit, &ctx);
    } else {
        bool is_binary = false;
        (void)read_lines(target, &grep_line, &ctx, is_binary);
    }
    char summary[48];
    (void)std::snprintf(summary, sizeof(summary), "%u match(es)%s", static_cast<unsigned>(ctx.matches),
                        (ctx.matches >= GREP_MAX) ? " (stopped at 300)" : "");
    bridge_resp(id, ctx.matches > 0U, summary);
}

void cmd_touch(uint8_t id, const Args& a)
{
    char target[PATH_LEN];
    if ((a.positional == 0U) || !arg_path(id, a, 0U, target) || (target[0] == '\0')) {
        if (a.positional == 0U) {
            bridge_resp(id, false, "usage: touch <file>");
        }
        return;
    }
    common::Status status = common::Status::OK;
    {
        fw::UiLock lock;
        status = fs()->touch(target);
    }
    bridge_resp(id, common::is_ok(status),
                common::is_ok(status) ? nullptr
                                      : ((status == common::Status::ERR_NOT_SUPPORTED) ? "not supported" : "failed"));
}

struct TreeCtx {
    uint8_t id;
    bool last_at[MAX_DEPTH + 1U];
    size_t printed;
    size_t dirs;
    size_t files;
};

bool tree_visit(const char* p_path, const hal::DirEntry& entry, uint8_t depth, bool is_last, void* p_ctx)
{
    (void)p_path;
    auto* const p_t = static_cast<TreeCtx*>(p_ctx);
    p_t->last_at[depth] = is_last;
    char prefix[LINE_LEN] = {};
    size_t used = 0U;
    for (uint8_t level = 0U; (level < depth) && (used < sizeof(prefix)); ++level) {
        used += static_cast<size_t>(
            std::snprintf(&prefix[used], sizeof(prefix) - used, "%s", p_t->last_at[level] ? "    " : "│   "));
    }
    line(p_t->id, "%s%s %s%s", prefix, is_last ? "└──" : "├──", entry.name,
         entry.is_dir ? "/" : "");
    p_t->dirs += entry.is_dir ? 1U : 0U;
    p_t->files += entry.is_dir ? 0U : 1U;
    return ++p_t->printed < TREE_MAX;
}

void cmd_tree(uint8_t id, const Args& a)
{
    char target[PATH_LEN];
    if (!arg_path(id, a, 0U, target)) {
        return;
    }
    char buf[PATH_LEN + 1U];
    line(id, "%s", shown(target, buf, sizeof(buf)));
    TreeCtx ctx{id, {}, 0U, 0U, 0U};
    const uint8_t depth = ((a.number > 0) && (a.number <= MAX_DEPTH)) ? static_cast<uint8_t>(a.number) : MAX_DEPTH;
    const bool complete = walk(target, depth, flag(a, 'a'), &tree_visit, &ctx);
    char summary[64];
    (void)std::snprintf(summary, sizeof(summary), "%u director%s, %u file(s)%s", static_cast<unsigned>(ctx.dirs),
                        (ctx.dirs == 1U) ? "y" : "ies", static_cast<unsigned>(ctx.files),
                        complete ? "" : " (stopped at 500 lines)");
    bridge_resp(id, true, summary);
}

void cmd_mount(uint8_t id)
{
    hal::MountInfo info{};
    common::Status status = common::Status::OK;
    {
        fw::UiLock lock;
        status = fs()->get_mount_info(info);
    }
    if (status == common::Status::ERR_NOT_SUPPORTED) {
        line(id, "sdcard on / (details not supported by this platform)");
        bridge_resp(id, true, nullptr);
        return;
    }
    line(id, "sdcard on / type %s (native %s)", info.fs_type, info.mount_point);
    line(id, "card     %s %s, %u-bit bus @ %u kHz", info.device, info.media, static_cast<unsigned>(info.bus_width),
         static_cast<unsigned>(info.freq_khz));
    line(id, "layout   sector %u B, cluster %u B", static_cast<unsigned>(info.sector_size),
         static_cast<unsigned>(info.cluster_size));
    bridge_resp(id, true, nullptr);
}

void cmd_format(uint8_t id, size_t argc, char* const p_args[])
{
    const bool confirmed = (argc > 1U) && (std::strcmp(p_args[1], "--yes") == 0);
    if (!confirmed) {
        line(id, "format ERASES THE WHOLE SD CARD: every installed app, app data and file.");
        bridge_resp(id, false, "type `format --yes` to confirm");
        return;
    }
    // Đóng mọi app (app script đang chạy có thể đang mở file trên thẻ) trước khi xóa.
    (void)bridge_run_on_ui([](void*) { fw::app_manager().home(); }, nullptr);
    common::Status status = common::Status::OK;
    {
        fw::UiLock lock;
        status = fs()->format();
        if (common::is_ok(status)) {
            for (const char* const p_dir : MOUNT_ROOT_DIRS) {
                (void)fs()->make_dir(p_dir);
            }
        }
    }
    s_cwd[0] = '\0';
    if (!common::is_ok(status)) {
        bridge_resp(id, false, (status == common::Status::ERR_NOT_SUPPORTED) ? "not supported" : "format failed");
        return;
    }
    bridge_resp(id, true, "formatted - rebooting so the app list matches the empty card");
    osal::delay_ms(REBOOT_DELAY_MS);
    s_p_dev->p_system->restart();
}

} // namespace

void bridge_fs_begin_session(const hal::Devices& devices)
{
    s_p_dev = &devices;
    s_files.bind(devices);
    s_cwd[0] = '\0';
}

bool bridge_fs_resolve(const char* p_in, char* p_out, size_t out_len)
{
    return normalize((p_in != nullptr) ? p_in : ".", p_out, out_len);
}

bool bridge_fs_command(uint8_t id, size_t argc, char* const p_args[], const hal::Devices& devices)
{
    if (argc == 0U) {
        return false;
    }
    if (s_p_dev == nullptr) {
        bridge_fs_begin_session(devices);
    }
    static const char* const COMMANDS[] = {"cd", "pwd", "ls", "mkdir", "rm", "rmdir", "cp", "mv", "cat", "head",
                                           "tail", "stat", "du", "df", "find", "grep", "touch", "tree", "mount",
                                           "format"};
    const char* const p_cmd = p_args[0];
    bool known = false;
    for (const char* const p_name : COMMANDS) {
        known = known || (std::strcmp(p_cmd, p_name) == 0);
    }
    if (!known) {
        return false;
    }
    if (fs() == nullptr) {
        bridge_resp(id, false, "no SD card");
        return true;
    }
    const Args a = parse(argc, p_args, 1U);
    const auto is = [p_cmd](const char* p_name) { return std::strcmp(p_cmd, p_name) == 0; };
    if (is("cd")) {
        cmd_cd(id, a);
    } else if (is("pwd")) {
        cmd_pwd(id);
    } else if (is("ls")) {
        cmd_ls(id, a);
    } else if (is("mkdir")) {
        cmd_mkdir(id, a);
    } else if (is("rm")) {
        cmd_rm(id, a, false);
    } else if (is("rmdir")) {
        cmd_rm(id, a, true);
    } else if (is("cp") || is("mv")) {
        cmd_cp_mv(id, a, is("mv"));
    } else if (is("cat") || is("head")) {
        cmd_cat_head(id, a, is("head"));
    } else if (is("tail")) {
        cmd_tail(id, a);
    } else if (is("stat")) {
        cmd_stat(id, a);
    } else if (is("du")) {
        cmd_du(id, a);
    } else if (is("df")) {
        cmd_df(id);
    } else if (is("find")) {
        cmd_find(id, a);
    } else if (is("grep")) {
        cmd_grep(id, a);
    } else if (is("touch")) {
        cmd_touch(id, a);
    } else if (is("tree")) {
        cmd_tree(id, a);
    } else if (is("mount")) {
        cmd_mount(id);
    } else {
        cmd_format(id, argc, p_args);
    }
    return true;
}

} // namespace runtime
