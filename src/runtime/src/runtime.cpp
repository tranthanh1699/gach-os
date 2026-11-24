/**
 * @file    runtime.cpp
 * @brief   Quản lý gói: cài (.gapp -> apps/<id>), gỡ, đăng ký slot app script với AppManager.
 */
#include "common/event.hpp"
#include "common/log.hpp"
#include "runtime/runtime.hpp"

#include <cstdio>
#include <cstring>

#include "common/string_utils.hpp"
#include "framework/app_manager.hpp"
#include "fs_utils.hpp"
#include "hal_api/board.hpp"
#include "manifest.hpp"
#include "package_archive.hpp"
#include "script_app.hpp"
#include "script_host.hpp"
#include "store.hpp"

namespace runtime {

namespace {

constexpr const char* APPS_DIR = "apps";
constexpr const char* INSTALL_DIR = "install";
constexpr const char* MANIFEST_FILE = "manifest.ini";
constexpr const char* DATA_FILE = "data.ini";
constexpr const char* PACKAGE_EXT = ".gapp";
constexpr size_t MANIFEST_BUF_LEN = 512U;
constexpr size_t DATA_BACKUP_LEN = 1024U;
constexpr size_t ERROR_BUF_LEN = 96U;
// Đủ chứa toàn bộ thư mục apps/ trong một lần liệt kê (không có API "đọc tiếp" thư mục);
// +1 để phát hiện thẻ có nhiều app hơn giới hạn.
constexpr size_t SCAN_BATCH = MAX_PACKAGES + 1U;

hal::IFileSystem* s_p_fs = nullptr;
fw::AppManager* s_p_apps = nullptr;
ScriptHost s_host;
ScriptApp s_slots[MAX_PACKAGES];
char s_manifest_buf[MANIFEST_BUF_LEN];
uint8_t s_data_backup[DATA_BACKUP_LEN];
char s_error[ERROR_BUF_LEN];

struct NameBatch {
    char names[SCAN_BATCH][hal::FS_NAME_MAX_LEN + 1U];
    size_t count;
    bool want_dirs;
    const char* p_suffix; /**< Chỉ lấy file có đuôi này (NULL = bỏ qua lọc). */
};

void set_error(const char* p_subject, const char* p_reason)
{
    (void)std::snprintf(s_error, sizeof(s_error), "%s: %s", p_subject, p_reason);
    EVENT_W("runtime", "%s", s_error);
    fault_record("runtime", s_error);
}

// Tĩnh (~8 KiB): không đặt trên stack của task main lúc boot.
NameBatch s_batch;

bool ends_with(const char* p_text, const char* p_suffix)
{
    const size_t text_len = std::strlen(p_text);
    const size_t suffix_len = std::strlen(p_suffix);
    return (text_len > suffix_len) && (std::strcmp(&p_text[text_len - suffix_len], p_suffix) == 0);
}

bool collect_name(const hal::DirEntry& entry, void* p_ctx)
{
    auto* const p_batch = static_cast<NameBatch*>(p_ctx);
    const bool type_ok = (entry.is_dir == p_batch->want_dirs);
    const bool suffix_ok = (p_batch->p_suffix == nullptr) || ends_with(entry.name, p_batch->p_suffix);
    if (type_ok && suffix_ok && (entry.name[0] != '.')) {
        (void)common::copy_string(p_batch->names[p_batch->count], sizeof(p_batch->names[0]), entry.name);
        p_batch->count++;
    }
    return p_batch->count < SCAN_BATCH;
}

ScriptApp* find_slot(const char* p_id)
{
    for (ScriptApp& slot : s_slots) {
        if (slot.is_bound() && (std::strcmp(slot.manifest().id, p_id) == 0)) {
            return &slot;
        }
    }
    return nullptr;
}

ScriptApp* free_slot()
{
    for (ScriptApp& slot : s_slots) {
        if (!slot.is_bound()) {
            return &slot;
        }
    }
    return nullptr;
}

/** @brief Đọc apps/<id>/manifest.ini và đăng ký slot với AppManager. */
common::Status register_installed(const char* p_id)
{
    if (find_slot(p_id) != nullptr) {
        return common::Status::OK; // Đã đăng ký.
    }
    char dir[PATH_BUF_LEN];
    char path[PATH_BUF_LEN];
    size_t len = 0U;
    if (!join_path(dir, sizeof(dir), APPS_DIR, p_id) || !join_path(path, sizeof(path), dir, MANIFEST_FILE)) {
        set_error(p_id, "path too long");
        return common::Status::ERR_PARAM;
    }
    common::Status status = read_small_file(*s_p_fs, path, s_manifest_buf, sizeof(s_manifest_buf), len);
    if (!common::is_ok(status)) {
        set_error(p_id, "cannot read manifest");
        return status;
    }
    Manifest manifest{};
    const char* p_err = "";
    status = parse_manifest(s_manifest_buf, len, manifest, &p_err);
    if (!common::is_ok(status) || (std::strcmp(manifest.id, p_id) != 0)) {
        set_error(p_id, common::is_ok(status) ? "id does not match folder" : p_err);
        return common::Status::ERR_PARAM;
    }
    ScriptApp* const p_slot = free_slot();
    if (p_slot == nullptr) {
        set_error(p_id, "too many apps installed");
        return common::Status::ERR_FULL;
    }
    p_slot->bind(manifest, s_host);
    status = s_p_apps->register_app(*p_slot);
    if (!common::is_ok(status)) {
        p_slot->unbind();
        set_error(p_id, "app registry full");
    }
    return status;
}

/** @brief Giữ lại data.ini khi nâng cấp app. @return số byte đã sao lưu. */
size_t backup_data(const char* p_app_dir)
{
    char path[PATH_BUF_LEN];
    size_t len = 0U;
    if (!join_path(path, sizeof(path), p_app_dir, DATA_FILE) ||
        !common::is_ok(read_small_file(*s_p_fs, path, reinterpret_cast<char*>(s_data_backup),
                                       sizeof(s_data_backup), len))) {
        len = 0U;
    }
    return len;
}

void restore_data(const char* p_app_dir, size_t len)
{
    char path[PATH_BUF_LEN];
    hal::FileHandle handle = hal::INVALID_FILE;
    if ((len > 0U) && join_path(path, sizeof(path), p_app_dir, DATA_FILE) &&
        common::is_ok(s_p_fs->open(path, hal::OpenMode::WRITE, handle))) {
        (void)s_p_fs->write(handle, s_data_backup, len);
        s_p_fs->close(handle);
    }
}

/** @brief Cài một gói: kiểm tra manifest -> (gỡ bản cũ, giữ dữ liệu) -> giải nén -> đăng ký -> xóa gói. */
common::Status install_package(const char* p_file_name, bool register_now)
{
    char archive[PATH_BUF_LEN];
    if (!join_path(archive, sizeof(archive), INSTALL_DIR, p_file_name)) {
        set_error(p_file_name, "path too long");
        return common::Status::ERR_PARAM;
    }
    size_t len = 0U;
    common::Status status =
        archive_read_entry(*s_p_fs, archive, MANIFEST_FILE, s_manifest_buf, sizeof(s_manifest_buf), len);
    if (!common::is_ok(status)) {
        set_error(p_file_name, (status == common::Status::ERR_NOT_FOUND) ? "no manifest.ini" : "corrupt package");
        return status;
    }
    Manifest manifest{};
    const char* p_err = "";
    status = parse_manifest(s_manifest_buf, len, manifest, &p_err);
    if (!common::is_ok(status)) {
        set_error(p_file_name, p_err);
        return status;
    }

    if ((find_slot(manifest.id) == nullptr) && (free_slot() == nullptr)) {
        set_error(manifest.name, "too many apps installed"); // Giữ gói trong install/, không giải nén.
        return common::Status::ERR_FULL;
    }
    if (find_slot(manifest.id) != nullptr) {
        status = s_p_apps->unregister_app(manifest.id);
        if (!common::is_ok(status)) {
            set_error(manifest.name, "close the app before updating");
            return status;
        }
        find_slot(manifest.id)->unbind();
    }

    char app_dir[PATH_BUF_LEN];
    (void)join_path(app_dir, sizeof(app_dir), APPS_DIR, manifest.id);
    const size_t data_len = backup_data(app_dir);
    status = remove_tree(*s_p_fs, app_dir);
    if (common::is_ok(status)) {
        status = make_dirs(*s_p_fs, app_dir);
    }
    if (common::is_ok(status)) {
        status = archive_extract(*s_p_fs, archive, app_dir);
    }
    if (!common::is_ok(status)) {
        set_error(p_file_name, (status == common::Status::ERR_PARAM) ? "unsafe path in package" : "extract failed");
        (void)remove_tree(*s_p_fs, app_dir); // Không để lại app cài dở.
        return status;
    }
    restore_data(app_dir, data_len);
    (void)s_p_fs->remove(archive);
    EVENT_I("runtime", "installed %s v%s (%s)", manifest.id, manifest.version, p_file_name);
    return register_now ? register_installed(manifest.id) : common::Status::OK;
}

common::Status install_all(bool register_now, size_t& installed)
{
    installed = 0U;
    common::Status first_error = common::Status::OK;
    // Mỗi vòng xử lý một batch; gói cài xong bị xóa nên lần liệt kê sau chỉ còn gói lỗi/chưa xử lý.
    for (size_t round = 0U; round < 4U; ++round) {
        NameBatch& batch = s_batch;
        batch.count = 0U;
        batch.want_dirs = false;
        batch.p_suffix = PACKAGE_EXT;
        if (!common::is_ok(s_p_fs->list_dir(INSTALL_DIR, &collect_name, &batch)) || (batch.count == 0U)) {
            break;
        }
        size_t progressed = 0U;
        for (size_t index = 0U; index < batch.count; ++index) {
            const common::Status status = install_package(batch.names[index], register_now);
            if (common::is_ok(status)) {
                ++installed;
                ++progressed;
            } else if (common::is_ok(first_error)) {
                first_error = status;
            } else {
                // Giữ lỗi đầu tiên.
            }
        }
        if (progressed == 0U) {
            break; // Chỉ còn gói lỗi -> dừng, tránh lặp vô hạn.
        }
    }
    return first_error;
}

} // namespace

common::Status init(const hal::Devices& devices, fw::AppManager& apps)
{
    s_error[0] = '\0';
    if ((devices.p_fs == nullptr) || !devices.p_fs->is_mounted() || (devices.app_arena.p_base == nullptr)) {
        return common::Status::ERR_NOT_SUPPORTED;
    }
    s_p_fs = devices.p_fs;
    s_p_apps = &apps;
    s_host.bind(devices.p_fs, devices.app_arena, devices.asset_arena, devices.p_http);
    store_init(devices);
    (void)make_dirs(*s_p_fs, APPS_DIR);
    (void)make_dirs(*s_p_fs, INSTALL_DIR);

    // Đăng ký app đã cài TRƯỚC, rồi mới cài gói mới -> kiểm tra được giới hạn slot trước khi giải nén.
    NameBatch& batch = s_batch;
    batch.count = 0U;
    batch.want_dirs = true;
    batch.p_suffix = nullptr;
    const common::Status status = s_p_fs->list_dir(APPS_DIR, &collect_name, &batch);
    for (size_t index = 0U; common::is_ok(status) && (index < batch.count); ++index) {
        (void)register_installed(batch.names[index]); // Một app hỏng không chặn app khác.
    }
    if (batch.count > MAX_PACKAGES) {
        set_error("apps", "too many apps on SD card");
    }

    size_t installed = 0U;
    (void)install_all(true, installed);
    return status;
}

bool is_available()
{
    return (s_p_fs != nullptr) && s_p_fs->is_mounted();
}

size_t package_count()
{
    size_t count = 0U;
    for (const ScriptApp& slot : s_slots) {
        count += slot.is_bound() ? 1U : 0U;
    }
    return count;
}

bool package_at(size_t index, PackageView& out)
{
    size_t seen = 0U;
    for (const ScriptApp& slot : s_slots) {
        if (slot.is_bound()) {
            if (seen == index) {
                out = PackageView{slot.manifest().id, slot.manifest().name, slot.manifest().version};
                return true;
            }
            ++seen;
        }
    }
    return false;
}

common::Status install_pending(size_t& installed)
{
    installed = 0U;
    if (!is_available()) {
        set_error("storage", "no SD card");
        return common::Status::ERR_NOT_SUPPORTED;
    }
    s_error[0] = '\0';
    return install_all(true, installed);
}

common::Status uninstall(const char* p_id)
{
    ScriptApp* const p_slot = find_slot(p_id);
    if (!is_available() || (p_slot == nullptr)) {
        return common::Status::ERR_PARAM;
    }
    common::Status status = s_p_apps->unregister_app(p_id);
    if (!common::is_ok(status)) {
        set_error(p_slot->manifest().name, "close the app before uninstalling");
        return status;
    }
    char app_dir[PATH_BUF_LEN];
    (void)join_path(app_dir, sizeof(app_dir), APPS_DIR, p_id);
    p_slot->unbind();
    status = remove_tree(*s_p_fs, app_dir);
    if (!common::is_ok(status)) {
        set_error(p_id, "remove failed");
    } else {
        EVENT_I("runtime", "uninstalled %s", p_id);
    }
    return status;
}

bool package_details(const char* p_id, PackageDetails& details)
{
    ScriptApp* const p_slot = find_slot(p_id);
    if (p_slot == nullptr) {
        return false;
    }
    const Manifest& m = p_slot->manifest();
    details = PackageDetails{m.id, m.name, m.version, m.entry, m.icon, m.sdk,
                             (m.permissions & PERMISSION_STORAGE) != 0U, (m.permissions & PERMISSION_NETWORK) != 0U};
    return true;
}

void script_stats(ScriptStats& stats)
{
    s_host.stats(stats);
}

bool script_mark_killed(const char* p_id)
{
    if ((p_id == nullptr) || (std::strcmp(s_host.running_id(), p_id) != 0)) {
        return false;
    }
    s_host.kill();
    return true;
}

const char* last_error()
{
    return s_error;
}

} // namespace runtime
