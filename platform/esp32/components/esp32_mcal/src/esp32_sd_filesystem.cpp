/**
 * @file    esp32_sd_filesystem.cpp
 * @brief   Hiện thực filesystem thẻ SD (esp_vfs_fat + sdspi / sdmmc).
 */
#include "esp32_mcal/esp32_sd_filesystem.hpp"

#include <cstdio>
#include <cstring>
#include <dirent.h>
#include <sys/stat.h>
#include <unistd.h>

#include "driver/sdspi_host.h"
#include "esp_vfs_fat.h"
#include "sdmmc_cmd.h"
#if SOC_SDMMC_HOST_SUPPORTED
#include "driver/sdmmc_host.h"
#endif
#if SOC_SDMMC_HOST_SUPPORTED && SOC_GP_LDO_SUPPORTED
#include "sd_pwr_ctrl_by_on_chip_ldo.h"
#endif

namespace esp32_mcal {

namespace {

constexpr const char* MOUNT_POINT = "/sd";
constexpr size_t NATIVE_PATH_LEN = hal::FS_PATH_MAX_LEN + 8U;
constexpr int VFS_MAX_FILES = 6;
constexpr size_t ALLOCATION_UNIT = 16U * 1024U;
constexpr mode_t DIR_MODE = 0775;

#if SOC_SDMMC_HOST_SUPPORTED
/** @brief Thay cho sdmmc_host_deinit khi mount lỗi: giữ controller cho slot khác đang dùng chung. */
esp_err_t keep_host_alive()
{
    return ESP_OK;
}
#endif

} // namespace

Esp32SdFileSystem::Esp32SdFileSystem() : m_files{}, m_p_card(nullptr), m_is_mounted(false) {}

common::Status Esp32SdFileSystem::mount(spi_host_device_t host, gpio_num_t cs)
{
    if (m_is_mounted) {
        return common::Status::ERR_STATE;
    }
    sdmmc_host_t host_cfg = SDSPI_HOST_DEFAULT();
    host_cfg.slot = host;
    sdspi_device_config_t slot_cfg = SDSPI_DEVICE_CONFIG_DEFAULT();
    slot_cfg.gpio_cs = cs;
    slot_cfg.host_id = host;

    esp_vfs_fat_sdmmc_mount_config_t mount_cfg{};
    mount_cfg.format_if_mount_failed = false; // Không bao giờ tự format thẻ của người dùng.
    mount_cfg.max_files = VFS_MAX_FILES;
    mount_cfg.allocation_unit_size = ALLOCATION_UNIT;

    if (esp_vfs_fat_sdspi_mount(MOUNT_POINT, &host_cfg, &slot_cfg, &mount_cfg, &m_p_card) != ESP_OK) {
        m_p_card = nullptr;
        return common::Status::ERR_IO;
    }
    m_is_mounted = true;
    return common::Status::OK;
}

#if SOC_SDMMC_HOST_SUPPORTED
common::Status Esp32SdFileSystem::mount_sdmmc(const SdmmcConfig& cfg)
{
    if (m_is_mounted) {
        return common::Status::ERR_STATE;
    }
    sdmmc_host_t host_cfg = SDMMC_HOST_DEFAULT();
    host_cfg.slot = cfg.slot;
    host_cfg.max_freq_khz = cfg.max_freq_khz;
    host_cfg.deinit = &keep_host_alive;

#if SOC_GP_LDO_SUPPORTED
    if (cfg.pwr_ldo_channel >= 0) {
        sd_pwr_ctrl_ldo_config_t ldo_cfg{};
        ldo_cfg.ldo_chan_id = cfg.pwr_ldo_channel;
        sd_pwr_ctrl_handle_t p_pwr = nullptr;
        if (sd_pwr_ctrl_new_on_chip_ldo(&ldo_cfg, &p_pwr) != ESP_OK) {
            return common::Status::ERR_IO;
        }
        host_cfg.pwr_ctrl_handle = p_pwr;
    }
#endif

    sdmmc_slot_config_t slot_cfg = SDMMC_SLOT_CONFIG_DEFAULT();
    slot_cfg.width = cfg.bus_width;

    esp_vfs_fat_sdmmc_mount_config_t mount_cfg{};
    mount_cfg.format_if_mount_failed = false; // Không bao giờ tự format thẻ của người dùng.
    mount_cfg.max_files = VFS_MAX_FILES;
    mount_cfg.allocation_unit_size = ALLOCATION_UNIT;

    if (esp_vfs_fat_sdmmc_mount(MOUNT_POINT, &host_cfg, &slot_cfg, &mount_cfg, &m_p_card) != ESP_OK) {
        m_p_card = nullptr;
#if SOC_GP_LDO_SUPPORTED
        if (host_cfg.pwr_ctrl_handle != nullptr) {
            (void)sd_pwr_ctrl_del_on_chip_ldo(host_cfg.pwr_ctrl_handle); // Trả kênh LDO khi không có thẻ.
        }
#endif
        return common::Status::ERR_IO;
    }
    m_is_mounted = true;
    return common::Status::OK;
}
#endif

bool Esp32SdFileSystem::to_native(const char* p_path, char* p_out, size_t out_len) const
{
    if ((p_path == nullptr) || (std::strlen(p_path) > hal::FS_PATH_MAX_LEN)) {
        return false;
    }
    const int written = std::snprintf(p_out, out_len, "%s/%s", MOUNT_POINT, p_path);
    return (written > 0) && (static_cast<size_t>(written) < out_len);
}

FILE* Esp32SdFileSystem::file_of(hal::FileHandle handle) const
{
    if ((handle < 0) || (static_cast<size_t>(handle) >= MAX_OPEN_FILES)) {
        return nullptr;
    }
    return m_files[handle];
}

common::Status Esp32SdFileSystem::open(const char* p_path, hal::OpenMode mode, hal::FileHandle& handle)
{
    handle = hal::INVALID_FILE;
    char native[NATIVE_PATH_LEN];
    if (!m_is_mounted || !to_native(p_path, native, sizeof(native))) {
        return m_is_mounted ? common::Status::ERR_PARAM : common::Status::ERR_STATE;
    }
    for (size_t index = 0U; index < MAX_OPEN_FILES; ++index) {
        if (m_files[index] == nullptr) {
            FILE* const p_file = std::fopen(native, (mode == hal::OpenMode::READ) ? "rb" : "wb");
            if (p_file == nullptr) {
                return (mode == hal::OpenMode::READ) ? common::Status::ERR_NOT_FOUND : common::Status::ERR_IO;
            }
            m_files[index] = p_file;
            handle = static_cast<hal::FileHandle>(index);
            return common::Status::OK;
        }
    }
    return common::Status::ERR_NO_MEM;
}

common::Status Esp32SdFileSystem::read(hal::FileHandle handle, uint8_t* p_buf, size_t len, size_t& read_len)
{
    read_len = 0U;
    FILE* const p_file = file_of(handle);
    if ((p_file == nullptr) || (p_buf == nullptr)) {
        return common::Status::ERR_PARAM;
    }
    read_len = std::fread(p_buf, 1U, len, p_file);
    return ((read_len < len) && (std::ferror(p_file) != 0)) ? common::Status::ERR_IO : common::Status::OK;
}

common::Status Esp32SdFileSystem::write(hal::FileHandle handle, const uint8_t* p_data, size_t len)
{
    FILE* const p_file = file_of(handle);
    if ((p_file == nullptr) || ((p_data == nullptr) && (len > 0U))) {
        return common::Status::ERR_PARAM;
    }
    return (std::fwrite(p_data, 1U, len, p_file) == len) ? common::Status::OK : common::Status::ERR_IO;
}

void Esp32SdFileSystem::close(hal::FileHandle handle)
{
    FILE* const p_file = file_of(handle);
    if (p_file != nullptr) {
        (void)std::fclose(p_file);
        m_files[handle] = nullptr;
    }
}

bool Esp32SdFileSystem::exists(const char* p_path, bool& is_dir)
{
    is_dir = false;
    char native[NATIVE_PATH_LEN];
    struct stat info {};
    if (!m_is_mounted || !to_native(p_path, native, sizeof(native)) || (::stat(native, &info) != 0)) {
        return false;
    }
    is_dir = S_ISDIR(info.st_mode);
    return true;
}

common::Status Esp32SdFileSystem::list_dir(const char* p_path, hal::DirCallback callback, void* p_ctx)
{
    char native[NATIVE_PATH_LEN];
    if (!m_is_mounted || (callback == nullptr) || !to_native(p_path, native, sizeof(native))) {
        return common::Status::ERR_PARAM;
    }
    DIR* const p_dir = opendir(native);
    if (p_dir == nullptr) {
        return common::Status::ERR_NOT_FOUND;
    }
    for (const struct dirent* p_ent = readdir(p_dir); p_ent != nullptr; p_ent = readdir(p_dir)) {
        if ((std::strcmp(p_ent->d_name, ".") == 0) || (std::strcmp(p_ent->d_name, "..") == 0) ||
            (std::strlen(p_ent->d_name) > hal::FS_NAME_MAX_LEN)) {
            continue;
        }
        hal::DirEntry entry{};
        (void)std::snprintf(entry.name, sizeof(entry.name), "%s", p_ent->d_name);
        entry.is_dir = (p_ent->d_type == DT_DIR);
        if (!callback(entry, p_ctx)) {
            break;
        }
    }
    (void)closedir(p_dir);
    return common::Status::OK;
}

common::Status Esp32SdFileSystem::make_dir(const char* p_path)
{
    char native[NATIVE_PATH_LEN];
    if (!m_is_mounted || !to_native(p_path, native, sizeof(native))) {
        return common::Status::ERR_PARAM;
    }
    bool is_dir = false;
    if (exists(p_path, is_dir)) {
        return is_dir ? common::Status::OK : common::Status::ERR_STATE;
    }
    return (mkdir(native, DIR_MODE) == 0) ? common::Status::OK : common::Status::ERR_IO;
}

common::Status Esp32SdFileSystem::remove(const char* p_path)
{
    char native[NATIVE_PATH_LEN];
    bool is_dir = false;
    if (!m_is_mounted || !to_native(p_path, native, sizeof(native))) {
        return common::Status::ERR_PARAM;
    }
    if (!exists(p_path, is_dir)) {
        return common::Status::OK;
    }
    const int result = is_dir ? rmdir(native) : unlink(native);
    return (result == 0) ? common::Status::OK : common::Status::ERR_IO;
}

common::Status Esp32SdFileSystem::get_capacity(uint64_t& total_bytes, uint64_t& free_bytes)
{
    if (!m_is_mounted) {
        return common::Status::ERR_STATE;
    }
    return (esp_vfs_fat_info(MOUNT_POINT, &total_bytes, &free_bytes) == ESP_OK) ? common::Status::OK
                                                                                 : common::Status::ERR_IO;
}

namespace {
void fill_from_stat(const struct stat& info, hal::DirEntry& entry)
{
    entry.is_dir = S_ISDIR(info.st_mode);
    entry.size = entry.is_dir ? 0U : static_cast<uint32_t>(info.st_size);
    entry.mtime = (info.st_mtime > 0) ? static_cast<uint32_t>(info.st_mtime) : 0U;
}
} // namespace

common::Status Esp32SdFileSystem::list_dir_detailed(const char* p_path, hal::DirCallback callback, void* p_ctx)
{
    char native[NATIVE_PATH_LEN];
    if (!m_is_mounted || (callback == nullptr) || !to_native(p_path, native, sizeof(native))) {
        return common::Status::ERR_PARAM;
    }
    DIR* const p_dir = opendir(native);
    if (p_dir == nullptr) {
        return common::Status::ERR_NOT_FOUND;
    }
    const size_t dir_len = std::strlen(native);
    char child[NATIVE_PATH_LEN + hal::FS_NAME_MAX_LEN + 2U];
    for (const struct dirent* p_ent = readdir(p_dir); p_ent != nullptr; p_ent = readdir(p_dir)) {
        if ((std::strcmp(p_ent->d_name, ".") == 0) || (std::strcmp(p_ent->d_name, "..") == 0) ||
            (std::strlen(p_ent->d_name) > hal::FS_NAME_MAX_LEN)) {
            continue;
        }
        hal::DirEntry entry{};
        (void)std::snprintf(entry.name, sizeof(entry.name), "%s", p_ent->d_name);
        entry.is_dir = (p_ent->d_type == DT_DIR);
        (void)std::snprintf(child, sizeof(child), "%.*s/%s", static_cast<int>(dir_len), native, p_ent->d_name);
        struct stat info {};
        if (::stat(child, &info) == 0) {
            fill_from_stat(info, entry);
        }
        if (!callback(entry, p_ctx)) {
            break;
        }
    }
    (void)closedir(p_dir);
    return common::Status::OK;
}

common::Status Esp32SdFileSystem::stat(const char* p_path, hal::DirEntry& entry)
{
    entry = hal::DirEntry{};
    char native[NATIVE_PATH_LEN];
    if (!m_is_mounted || !to_native(p_path, native, sizeof(native))) {
        return common::Status::ERR_PARAM;
    }
    struct stat info {};
    if (::stat(native, &info) != 0) {
        return common::Status::ERR_NOT_FOUND;
    }
    const char* const p_slash = std::strrchr(p_path, '/');
    (void)std::snprintf(entry.name, sizeof(entry.name), "%s", (p_slash != nullptr) ? (p_slash + 1) : p_path);
    fill_from_stat(info, entry);
    return common::Status::OK;
}

common::Status Esp32SdFileSystem::rename(const char* p_from, const char* p_to)
{
    char from[NATIVE_PATH_LEN];
    char to[NATIVE_PATH_LEN];
    if (!m_is_mounted || !to_native(p_from, from, sizeof(from)) || !to_native(p_to, to, sizeof(to))) {
        return common::Status::ERR_PARAM;
    }
    struct stat info {};
    if (::stat(to, &info) == 0) {
        return common::Status::ERR_STATE; // Không ghi đè.
    }
    return (std::rename(from, to) == 0) ? common::Status::OK : common::Status::ERR_IO;
}

} // namespace esp32_mcal
