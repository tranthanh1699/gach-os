/**
 * @file    filesystem.hpp
 * @brief   HAL contract: hệ thống file (thẻ SD, flash nội...) với đường dẫn tương đối từ gốc.
 *
 * Đường dẫn dùng '/' và tương đối với gốc của volume, VD: "apps/hello/main.lua".
 * Platform tự ánh xạ sang mount point thật (VD: "/sd/apps/hello/main.lua" trên ESP-IDF).
 */
#ifndef HAL_API_FILESYSTEM_HPP
#define HAL_API_FILESYSTEM_HPP

#include <cstddef>
#include <cstdint>

#include "common/status.hpp"

namespace hal {

constexpr size_t FS_PATH_MAX_LEN = 127U; /**< Độ dài đường dẫn tối đa (không gồm '\0'). */
constexpr size_t FS_NAME_MAX_LEN = 63U;  /**< Độ dài tên một mục tối đa. */

/** @brief Handle file mở. Giá trị âm = không hợp lệ. */
using FileHandle = int32_t;
constexpr FileHandle INVALID_FILE = -1;

/** @brief Chế độ mở file. */
enum class OpenMode : uint8_t {
    READ,  /**< Chỉ đọc, file phải tồn tại. */
    WRITE  /**< Tạo mới / ghi đè (truncate). */
};

/** @brief Thông tin một mục trong thư mục. */
struct DirEntry {
    char name[FS_NAME_MAX_LEN + 1U];
    bool is_dir;
    uint32_t size;  /**< Byte (0 với thư mục). Chỉ có giá trị với list_dir_detailed()/stat(). */
    uint32_t mtime; /**< Lần sửa cuối, giây UTC kể từ 1970 (0 = không rõ). Như size. */
};

/**
 * @brief Callback liệt kê thư mục.
 * @return false để dừng liệt kê sớm.
 */
using DirCallback = bool (*)(const DirEntry& entry, void* p_ctx);

/** @brief Thông tin phân vùng đang mount (lệnh `mount`). */
struct MountInfo {
    char mount_point[16]; /**< Đường dẫn native, VD "/sd". */
    char fs_type[16];     /**< VD "FAT32", "exFAT". */
    char device[16];      /**< Tên thẻ / thiết bị (CID). */
    char media[16];       /**< VD "SDHC", "SDXC", "SDSC", "MMC". */
    uint8_t bus_width;    /**< Số dây dữ liệu (1, 4, 8), 0 = không rõ. */
    uint32_t freq_khz;    /**< Tần số bus đang chạy. */
    uint32_t sector_size; /**< Byte / sector. */
    uint32_t cluster_size; /**< Byte / cluster (đơn vị cấp phát), 0 = không rõ. */
};

/** @brief Hệ thống file. Không yêu cầu thread-safe (chỉ gọi từ một task tại một thời điểm). */
class IFileSystem {
public:
    virtual ~IFileSystem() = default;

    /** @return true nếu volume đã mount và dùng được. */
    virtual bool is_mounted() const = 0;

    /** @brief Mở file. @param[out] handle Handle khi thành công. */
    virtual common::Status open(const char* p_path, OpenMode mode, FileHandle& handle) = 0;

    /**
     * @brief Đọc tối đa `len` byte.
     * @param[out] read_len Số byte thực đọc (0 = hết file).
     */
    virtual common::Status read(FileHandle handle, uint8_t* p_buf, size_t len, size_t& read_len) = 0;

    /** @brief Ghi toàn bộ `len` byte. */
    virtual common::Status write(FileHandle handle, const uint8_t* p_data, size_t len) = 0;

    /** @brief Đóng file (an toàn với INVALID_FILE). */
    virtual void close(FileHandle handle) = 0;

    /** @brief Kiểm tra tồn tại. @param[out] is_dir true nếu là thư mục. */
    virtual bool exists(const char* p_path, bool& is_dir) = 0;

    /** @brief Liệt kê các mục trong thư mục (không đệ quy, bỏ "." và ".."). */
    virtual common::Status list_dir(const char* p_path, DirCallback callback, void* p_ctx) = 0;

    /** @brief Tạo một thư mục (thư mục cha phải tồn tại; đã tồn tại -> OK). */
    virtual common::Status make_dir(const char* p_path) = 0;

    /** @brief Xóa file hoặc thư mục RỖNG. */
    virtual common::Status remove(const char* p_path) = 0;

    /**
     * @brief Như list_dir() nhưng điền cả size và mtime (chậm hơn: một lần stat mỗi mục).
     * @note  Mặc định: gọi list_dir() (size/mtime = 0).
     */
    virtual common::Status list_dir_detailed(const char* p_path, DirCallback callback, void* p_ctx)
    {
        return list_dir(p_path, callback, p_ctx);
    }

    /**
     * @brief Thông tin một file / thư mục.
     * @param[out] entry name = phần cuối đường dẫn, is_dir, size, mtime.
     * @return ERR_NOT_FOUND nếu không tồn tại, ERR_NOT_SUPPORTED nếu không hỗ trợ.
     */
    virtual common::Status stat(const char* p_path, DirEntry& entry)
    {
        (void)p_path;
        (void)entry;
        return common::Status::ERR_NOT_SUPPORTED;
    }

    /**
     * @brief Đổi tên / di chuyển trong cùng hệ thống file (đích không được tồn tại).
     * @return ERR_STATE nếu đích đã tồn tại, ERR_NOT_SUPPORTED nếu không hỗ trợ.
     */
    virtual common::Status rename(const char* p_from, const char* p_to)
    {
        (void)p_from;
        (void)p_to;
        return common::Status::ERR_NOT_SUPPORTED;
    }

    /** @param[out] info Thông tin mount / thẻ nhớ. */
    virtual common::Status get_mount_info(MountInfo& info)
    {
        (void)info;
        return common::Status::ERR_NOT_SUPPORTED;
    }

    /** @brief Như `touch`: tạo file rỗng nếu chưa có, có rồi thì cập nhật ngày sửa về hiện tại. */
    virtual common::Status touch(const char* p_path)
    {
        (void)p_path;
        return common::Status::ERR_NOT_SUPPORTED;
    }

    /**
     * @brief XÓA SẠCH và format lại phân vùng (vẫn mount sau khi xong).
     * @warning Mất toàn bộ dữ liệu. Người gọi phải đóng mọi file đang mở trước.
     */
    virtual common::Status format() { return common::Status::ERR_NOT_SUPPORTED; }

    /**
     * @brief Dung lượng phân vùng (byte).
     * @note  Lần gọi đầu có thể chậm (FAT phải đếm cụm trống), các lần sau dùng giá trị cache của FS.
     * @return ERR_NOT_SUPPORTED nếu không hỗ trợ, lỗi khác nếu chưa mount.
     */
    virtual common::Status get_capacity(uint64_t& total_bytes, uint64_t& free_bytes)
    {
        (void)total_bytes;
        (void)free_bytes;
        return common::Status::ERR_NOT_SUPPORTED;
    }
};

} // namespace hal

#endif /* HAL_API_FILESYSTEM_HPP */
