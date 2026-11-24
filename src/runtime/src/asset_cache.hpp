/**
 * @file    asset_cache.hpp
 * @brief   [Private] Kho ảnh của app script đang chạy: nạp file .gimg (đổi từ PNG lúc đóng gói) vào vùng nhớ cố
 *          định (asset arena của board), tra theo đường dẫn, thu hồi toàn bộ khi app đóng.
 *
 * Không phụ thuộc LVGL / Lua (test được trên máy tính). Định dạng .gimg: xem tools/gapp_pack.py.
 */
#ifndef RUNTIME_ASSET_CACHE_HPP
#define RUNTIME_ASSET_CACHE_HPP

#include <cstddef>
#include <cstdint>

#include "common/non_copyable.hpp"
#include "common/status.hpp"
#include "hal_api/filesystem.hpp"
#include "hal_api/board.hpp"

namespace runtime {

/** @brief Một ảnh đã nạp. Dữ liệu: màu RGB565 (height * stride) rồi alpha A8 (width * height) nếu has_alpha. */
struct AssetImage {
    const uint8_t* p_data;
    uint32_t data_size;
    uint16_t width;
    uint16_t height;
    uint16_t stride;   /**< Byte mỗi dòng của phần màu. */
    uint16_t index;    /**< Vị trí trong kho (0..MAX_IMAGES-1), ổn định tới lần reset kế tiếp. */
    bool has_alpha;
};

/** @brief Kho ảnh. Không thread-safe (chỉ task UI dùng). */
class AssetCache : private common::NonCopyable {
public:
    static constexpr size_t MAX_IMAGES = 256U;
    static constexpr size_t PATH_MAX_LEN = 63U;

    AssetCache() = default;

    /** @brief Gắn vùng nhớ và thu hồi toàn bộ (gọi khi app mở / đóng). region rỗng = không hỗ trợ ảnh. */
    void reset(const hal::MemoryRegion& region);

    /**
     * @brief Nạp ảnh của app (đã nạp thì trả lại bản cũ).
     * @param[in]  p_app_dir Thư mục app trên thẻ (VD "apps/org.x").
     * @param[in]  p_path    Đường dẫn tương đối trong app, đuôi .png hoặc .gimg (luôn đọc file .gimg).
     * @param[out] p_out     Ảnh trong kho.
     * @return ERR_NOT_FOUND (không có file), ERR_PARAM (đường dẫn / file sai định dạng), ERR_NO_MEM (kho đầy),
     *         ERR_NOT_SUPPORTED (không có vùng nhớ), ERR_IO (lỗi đọc).
     */
    common::Status load(hal::IFileSystem& fs, const char* p_app_dir, const char* p_path, const AssetImage*& p_out);

    /** @brief Ảnh theo index (NULL nếu không có). */
    const AssetImage* find_index(size_t index) const;

    /** @brief Cấp vùng nhớ thô (canvas, sprite tạm). @return NULL nếu đầy. */
    void* alloc(size_t bytes, size_t align);

    size_t used() const { return m_used; }
    size_t capacity() const { return m_region.size; }
    size_t image_count() const { return m_count; }

private:
    struct Entry {
        char path[PATH_MAX_LEN + 1U];
        AssetImage image;
    };

    hal::MemoryRegion m_region{nullptr, 0U};
    size_t m_used = 0U;
    size_t m_count = 0U;
    Entry m_entries[MAX_IMAGES] = {};
};

} // namespace runtime

#endif /* RUNTIME_ASSET_CACHE_HPP */
