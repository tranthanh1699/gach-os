/**
 * @file    log_history.hpp
 * @brief   [Private] Lịch sử log dạng vòng (ring buffer) cho `gach dmesg`: giữ log cả khi chưa có máy tính kết nối.
 */
#ifndef RUNTIME_LOG_HISTORY_HPP
#define RUNTIME_LOG_HISTORY_HPP

#include <cstddef>
#include <cstdint>

namespace runtime {
namespace log_history {

constexpr size_t CAPACITY = 32U * 1024U; /**< Byte log gần nhất được giữ (nằm trong PSRAM cùng .bss runtime). */

/** @brief Ghi thêm (mọi task, có khóa). Dữ liệu cũ nhất bị đè khi đầy. */
void append(const char* p_text, size_t len);

/** @return Tổng số byte đã từng ghi (vị trí ghi tiếp theo, tăng dần). */
uint32_t total();

/**
 * @brief Đọc tiếp từ vị trí pos (tính theo tổng byte đã ghi); pos cũ hơn phần còn giữ được tự nhảy tới byte cũ nhất.
 * @param[in,out] pos   Vị trí đọc, tăng theo số byte trả về.
 * @param[out]    p_out Bộ đệm nhận.
 * @return Số byte đã chép (0 = hết).
 */
size_t read(uint32_t& pos, char* p_out, size_t max_len);

} // namespace log_history
} // namespace runtime

#endif /* RUNTIME_LOG_HISTORY_HPP */
