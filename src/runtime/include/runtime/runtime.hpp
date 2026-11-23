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
