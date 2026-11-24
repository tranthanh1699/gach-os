/**
 * @file    asset_cache.cpp
 * @brief   Nạp ảnh .gimg vào kho ảnh của app.
 */
#include "asset_cache.hpp"

#include <cstdio>
#include <cstring>

#include "common/string_utils.hpp"

namespace runtime {

namespace {

constexpr size_t HEADER_LEN = 16U;
constexpr uint8_t GIMG_VERSION = 1U;
constexpr uint8_t FORMAT_RGB565 = 0U;
constexpr uint8_t FORMAT_RGB565A8 = 1U;
constexpr size_t RGB565_BYTES = 2U;
constexpr size_t DATA_ALIGN = 4U;
constexpr size_t FULL_PATH_LEN = 160U;
constexpr const char* PNG_EXT = ".png";
constexpr const char* GIMG_EXT = ".gimg";

uint16_t read_u16(const uint8_t* p_bytes)
{
    return static_cast<uint16_t>(p_bytes[0] | (static_cast<uint16_t>(p_bytes[1]) << 8U));
}

bool ends_with(const char* p_text, const char* p_suffix)
{
    const size_t len = std::strlen(p_text);
    const size_t suffix_len = std::strlen(p_suffix);
    return (len >= suffix_len) && (std::strcmp(&p_text[len - suffix_len], p_suffix) == 0);
}

/** @brief Đường dẫn an toàn: tương đối, không "..", không rỗng. */
bool is_safe(const char* p_path)
{
    return (p_path[0] != '\0') && (p_path[0] != '/') && (std::strstr(p_path, "..") == nullptr) &&
           (std::strlen(p_path) <= AssetCache::PATH_MAX_LEN);
}

/** @brief "<dir>/<path đổi .png -> .gimg>". */
bool gimg_path(const char* p_app_dir, const char* p_path, char* p_out, size_t out_len)
{
    const size_t path_len = std::strlen(p_path);
    int written = 0;
    if (ends_with(p_path, PNG_EXT)) {
        written = std::snprintf(p_out, out_len, "%s/%.*s%s", p_app_dir, static_cast<int>(path_len - 4U), p_path,
                                GIMG_EXT);
    } else if (ends_with(p_path, GIMG_EXT)) {
        written = std::snprintf(p_out, out_len, "%s/%s", p_app_dir, p_path);
    } else {
        return false;
    }
    return (written > 0) && (static_cast<size_t>(written) < out_len);
}

} // namespace

void AssetCache::reset(const hal::MemoryRegion& region)
{
    m_region = region;
    m_used = 0U;
    m_count = 0U;
}

void* AssetCache::alloc(size_t bytes, size_t align)
{
    if ((m_region.p_base == nullptr) || (align == 0U)) {
        return nullptr;
    }
    const auto base = reinterpret_cast<uintptr_t>(m_region.p_base);
    const uintptr_t start = (base + m_used + align - 1U) & ~(static_cast<uintptr_t>(align) - 1U);
    const size_t offset = static_cast<size_t>(start - base);
    if ((offset > m_region.size) || (bytes > (m_region.size - offset))) {
        return nullptr;
    }
    m_used = offset + bytes;
    return reinterpret_cast<void*>(start);
}

const AssetImage* AssetCache::find_index(size_t index) const
{
    return (index < m_count) ? &m_entries[index].image : nullptr;
}

common::Status AssetCache::load(hal::IFileSystem& fs, const char* p_app_dir, const char* p_path,
                                const AssetImage*& p_out)
{
    p_out = nullptr;
    if (m_region.p_base == nullptr) {
        return common::Status::ERR_NOT_SUPPORTED;
    }
    if ((p_path == nullptr) || !is_safe(p_path)) {
        return common::Status::ERR_PARAM;
    }
    for (size_t index = 0U; index < m_count; ++index) {
        if (std::strcmp(m_entries[index].path, p_path) == 0) {
            p_out = &m_entries[index].image;
            return common::Status::OK;
        }
    }
    char full[FULL_PATH_LEN];
    if (!gimg_path(p_app_dir, p_path, full, sizeof(full))) {
        return common::Status::ERR_PARAM;
    }
    if (m_count == MAX_IMAGES) {
        return common::Status::ERR_NO_MEM;
    }
    hal::FileHandle handle = hal::INVALID_FILE;
    if (!common::is_ok(fs.open(full, hal::OpenMode::READ, handle))) {
        return common::Status::ERR_NOT_FOUND;
    }
    uint8_t header[HEADER_LEN];
    size_t got = 0U;
    common::Status status = fs.read(handle, header, sizeof(header), got);
    const uint8_t format = header[5];
    const uint16_t width = read_u16(&header[8]);
    const uint16_t height = read_u16(&header[10]);
    const uint16_t stride = read_u16(&header[12]);
    const bool valid = common::is_ok(status) && (got == HEADER_LEN) && (std::memcmp(header, "GIMG", 4U) == 0) &&
                       (header[4] == GIMG_VERSION) && ((format == FORMAT_RGB565) || (format == FORMAT_RGB565A8)) &&
                       (width > 0U) && (height > 0U) && (stride >= (width * RGB565_BYTES));
    if (!valid) {
        fs.close(handle);
        return common::is_ok(status) ? common::Status::ERR_PARAM : common::Status::ERR_IO;
    }
    const bool has_alpha = (format == FORMAT_RGB565A8);
    const size_t size = (static_cast<size_t>(height) * stride) + (has_alpha ? (static_cast<size_t>(width) * height) : 0U);
    const size_t mark = m_used;
    auto* const p_data = static_cast<uint8_t*>(alloc(size, DATA_ALIGN));
    if (p_data == nullptr) {
        fs.close(handle);
        return common::Status::ERR_NO_MEM;
    }
    status = fs.read(handle, p_data, size, got);
    fs.close(handle);
    if (!common::is_ok(status) || (got != size)) {
        m_used = mark; // Trả lại chỗ đã cấp.
        return common::is_ok(status) ? common::Status::ERR_PARAM : common::Status::ERR_IO;
    }
    Entry& entry = m_entries[m_count];
    (void)common::copy_string(entry.path, sizeof(entry.path), p_path);
    entry.image = AssetImage{p_data, static_cast<uint32_t>(size), width, height, stride,
                             static_cast<uint16_t>(m_count), has_alpha};
    ++m_count;
    p_out = &entry.image;
    return common::Status::OK;
}

} // namespace runtime
