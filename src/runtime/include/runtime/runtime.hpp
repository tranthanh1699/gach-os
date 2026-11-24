/**
 * @file    runtime.hpp
 * @brief   Runtime app cài thêm (gói .gapp chạy trên máy ảo Lua) – API public.
 *
 * Bố cục trên filesystem (thẻ SD):
 *   install/<tên>.gapp     gói chờ cài (chép từ PC hoặc tải về); cài xong sẽ bị xóa
 *   apps/<id>/             app đã cài: manifest.ini, entry .lua, data.ini (gach.storage)
 */
#ifndef RUNTIME_RUNTIME_HPP
#define RUNTIME_RUNTIME_HPP

#include <cstddef>

#include "common/status.hpp"

namespace hal {
struct Devices;
}
namespace fw {
class AppManager;
}

namespace runtime {

constexpr size_t MAX_PACKAGES = 128U;      /**< Số app cài thêm tối đa (mỗi slot ~0.2 KiB RAM tĩnh + 64 B bộ đệm quét). */
constexpr size_t PACKAGE_ID_BUF_LEN = 32U; /**< Id gói tối đa 31 ký tự + ký tự kết thúc. */

/** @brief Thông tin hiển thị của một gói đã cài. */
struct PackageView {
    const char* p_id;
    const char* p_name;
    const char* p_version;
};

/** @brief Chi tiết manifest của một gói đã cài. */
struct PackageDetails {
    const char* p_id;
    const char* p_name;
    const char* p_version;
    const char* p_entry;
    const char* p_icon;
    uint8_t sdk;
    bool can_storage; /**< permissions=storage */
    bool can_network; /**< permissions=network */
};

/** @brief Trạng thái máy ảo Lua (một app script chạy tại một thời điểm). */
struct ScriptStats {
    bool is_running;      /**< Có app script đang mở. */
    const char* p_app_id; /**< Id app đang chạy ("" nếu không có). */
    bool is_faulted;      /**< App đang hiện màn hình lỗi. */
    const char* p_error;  /**< Lỗi gần nhất của app ("" nếu không có). */
    size_t mem_used;      /**< Byte arena Lua đang dùng. */
    size_t mem_peak;
    size_t mem_capacity;
    size_t widgets;       /**< Widget app đã tạo còn sống. */
    size_t timers;
    size_t handlers;      /**< Callback gach.ui.on. */
};

/** @return true nếu id là gói đã cài; điền details (chuỗi trỏ vào bộ nhớ runtime, dùng ngay). */
bool package_details(const char* p_id, PackageDetails& details);

/** @param[out] stats Trạng thái máy ảo Lua hiện tại. */
void script_stats(ScriptStats& stats);

/**
 * @brief Đánh dấu app script đang chạy (nếu đúng id) là bị buộc dừng: lần đóng kế tiếp KHÔNG chạy on_pause /
 *        on_destroy của app (dùng khi app treo / lỗi). Gọi từ UI task, ngay trước AppManager::close().
 * @return false nếu app đó không phải app script đang chạy.
 */
bool script_mark_killed(const char* p_id);

/** @brief Cắm backend trace + sổ sự kiện. Gọi đầu tiên trong entry point (trước mọi log / sự kiện). */
void diag_install();

/** @brief Sau board_init: nạp sổ lỗi (NVS), ghi lỗi nếu lần reset vừa rồi bất thường / có core dump. */
void diag_boot(const hal::Devices& devices);

/** @brief Ghi một lỗi vào sổ lỗi bền (`gach fault list`). code: "reset", "script", "install"... */
void fault_record(const char* p_code, const char* p_text);

/**
 * @brief Khởi tạo runtime: cài các gói chờ trong install/, đăng ký mọi app trong apps/.
 * @note  Gọi từ entry point của platform, trước fw::start().
 * @return ERR_NOT_SUPPORTED nếu board không có filesystem hoặc arena.
 */
common::Status init(const hal::Devices& devices, fw::AppManager& apps);

/** @return true nếu runtime dùng được (có thẻ nhớ + arena). */
bool is_available();

/** @return Số gói đã cài. */
size_t package_count();

/** @brief Thông tin gói thứ `index`. @return false nếu ngoài dải. */
bool package_at(size_t index, PackageView& out);

/**
 * @brief Cài mọi gói .gapp đang chờ trong install/ (chạy trong UI task, có thể mất vài giây).
 * @param[out] installed Số gói cài thành công.
 * @return OK nếu không có lỗi; lỗi đầu tiên gặp phải (chi tiết: last_error()).
 */
common::Status install_pending(size_t& installed);

/** @brief Gỡ cài đặt (app phải đang không mở). */
common::Status uninstall(const char* p_id);

/** @return Mô tả lỗi gần nhất (chuỗi rỗng nếu không có). */
const char* last_error();

/**
 * @brief Khởi động debug bridge (tool `gach` trên máy tính: cài app, shell, đẩy/kéo file, logcat).
 * @return ERR_NOT_SUPPORTED nếu board không có cổng debug.
 */
common::Status bridge_start(const hal::Devices& devices);

/**
 * @brief Gửi một đoạn log qua bridge (gọi từ hook log của platform, mọi task).
 * @return false nếu không có máy tính đang kết nối -> người gọi tự in log dạng chữ thô.
 */
bool bridge_log(const char* p_text, size_t len);

/**
 * @brief Ghi một đoạn log vào lịch sử (`gach dmesg`), kể cả khi chưa có máy tính kết nối.
 * @note  Platform gọi cho MỌI dòng log (gach log + log của SDK chip), từ mọi task (không gọi trong ISR).
 */
void log_history_append(const char* p_text, size_t len);

} // namespace runtime

#endif /* RUNTIME_RUNTIME_HPP */
