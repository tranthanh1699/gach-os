/**
 * @file    file_service.hpp
 * @brief   Dịch vụ file cho app tích hợp (File Manager...): duyệt, thông tin, tạo / đổi tên, đọc / ghi file nhỏ,
 *          và job chép / xóa cây thư mục chạy từng bước (không làm đơ giao diện).
 *
 * Đường dẫn tương đối gốc bộ nhớ ngoài (thẻ SD), phân cách '/', "" = gốc. Mọi hàm gọi từ UI task.
 * Không lộ kiểu HAL ra tầng app.
 */
#ifndef FRAMEWORK_FILE_SERVICE_HPP
#define FRAMEWORK_FILE_SERVICE_HPP

#include <cstddef>
#include <cstdint>

#include "common/non_copyable.hpp"
#include "common/status.hpp"

namespace hal {
struct Devices;
}

namespace fw {

constexpr size_t FILE_NAME_MAX_LEN = 63U;  /**< Tên một mục (không gồm '\0'). */
constexpr size_t FILE_PATH_MAX_LEN = 127U; /**< Đường dẫn đầy đủ (không gồm '\0'). */
constexpr size_t FILE_PATH_BUF_LEN = FILE_PATH_MAX_LEN + 1U;

/** @brief Một file / thư mục. */
struct FileEntry {
    char name[FILE_NAME_MAX_LEN + 1U];
    bool is_dir;
    uint32_t size;  /**< Byte (0 với thư mục). */
    uint32_t mtime; /**< Giây UTC kể từ 1970, 0 = không rõ. */
};

/** @brief Callback liệt kê; trả false để dừng sớm. */
using FileVisitor = bool (*)(const FileEntry& entry, void* p_ctx);

/** @brief Trạng thái job chép / xóa. */
enum class FileJobState : uint8_t {
    IDLE,    /**< Không có job. */
    RUNNING, /**< Gọi step() tiếp. */
    DONE,    /**< Xong; gọi start_*() để bắt đầu job mới. */
    FAILED   /**< Lỗi; xem job_error(). */
};

/** @brief Dịch vụ file. */
class FileService : private common::NonCopyable {
public:
    static constexpr uint8_t MAX_DEPTH = 8U; /**< Độ sâu thư mục tối đa của job chép / xóa. */

    FileService();

    /** @brief [Nội bộ framework] Gắn thiết bị của board. */
    void bind(const hal::Devices& devices);

    /** @return true nếu có bộ nhớ ngoài đã mount. */
    bool is_available() const;

    /** @brief Dung lượng (byte). @return false nếu không đọc được. */
    bool capacity(uint64_t& total_bytes, uint64_t& free_bytes) const;

    /** @brief Liệt kê thư mục kèm kích thước + ngày sửa. */
    common::Status list(const char* p_dir, FileVisitor visitor, void* p_ctx) const;

    /** @brief Thông tin một mục. @return ERR_NOT_FOUND nếu không có. */
    common::Status stat(const char* p_path, FileEntry& entry) const;

    /** @return true nếu đường dẫn tồn tại. */
    bool exists(const char* p_path) const;

    /** @brief Tạo một thư mục (cha phải tồn tại). */
    common::Status make_dir(const char* p_path);

    /** @brief Đổi tên / di chuyển (đích không được tồn tại). */
    common::Status rename(const char* p_from, const char* p_to);

    /**
     * @brief Đọc tối đa cap byte đầu file (file lớn hơn: chỉ phần đầu).
     * @param[out] len Số byte đã đọc.
     */
    common::Status read_file(const char* p_path, uint8_t* p_buf, size_t cap, size_t& len) const;

    /** @brief Ghi đè cả file. */
    common::Status write_file(const char* p_path, const uint8_t* p_data, size_t len);

    // ---- Job dài (một job tại một thời điểm) ----

    /**
     * @brief Bắt đầu chép file / cả thư mục sang đường dẫn đích (đích chưa tồn tại).
     * @return ERR_STATE nếu đang có job, ERR_PARAM nếu chép thư mục vào chính nó.
     */
    common::Status start_copy(const char* p_from, const char* p_to);

    /** @brief Bắt đầu xóa file / cả cây thư mục. */
    common::Status start_delete(const char* p_path);

    /**
     * @brief Chạy job trong khoảng budget_ms (gọi định kỳ từ UI task, VD lv_timer).
     * @return Trạng thái sau bước này.
     */
    FileJobState step(uint32_t budget_ms);

    /** @brief Hủy job đang chạy (phần đã chép / xóa giữ nguyên). */
    void cancel_job();

    FileJobState job_state() const { return m_job.state; }
    common::Status job_error() const { return m_job.error; }
    uint32_t job_items() const { return m_job.items; }  /**< Số file / thư mục đã xử lý. */
    uint32_t job_bytes() const { return m_job.bytes; }  /**< Số byte đã chép. */

    // ---- Tiện ích đường dẫn ----

    /** @brief p_dir + "/" + p_name ("" dir -> chỉ tên). @return false nếu quá dài. */
    static bool join(char* p_out, size_t out_len, const char* p_dir, const char* p_name);

    /** @brief Tên hợp lệ: không rỗng, không '/', không "." / "..", không ký tự điều khiển, <= FILE_NAME_MAX_LEN. */
    static bool is_valid_name(const char* p_name);

    /** @return Phần tên cuối của đường dẫn. */
    static const char* base_name(const char* p_path);

private:
    enum class JobKind : uint8_t { COPY, DELETE };
    enum class Phase : uint8_t { SCAN, COPY_FILE };

    struct Job {
        FileJobState state;
        JobKind kind;
        Phase phase;
        common::Status error;
        uint8_t depth;
        uint16_t next_index[MAX_DEPTH + 1U];
        char src[FILE_PATH_BUF_LEN];
        char dst[FILE_PATH_BUF_LEN];
        int32_t src_file;
        int32_t dst_file;
        uint32_t items;
        uint32_t bytes;
    };

    bool step_copy();
    bool step_delete();
    bool child_at(const char* p_dir, uint16_t index, FileEntry& out) const;
    bool push_child(const char* p_name);
    void pop_child();
    void close_job_files();
    void fail(common::Status status);

    const hal::Devices* m_p_devices;
    Job m_job;
};

} // namespace fw

#endif /* FRAMEWORK_FILE_SERVICE_HPP */
