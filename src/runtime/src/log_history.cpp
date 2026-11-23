/**
 * @file    log_history.cpp
 * @brief   Ring buffer lịch sử log.
 */
#include "log_history.hpp"

#include <cstring>

#include "osal/osal.hpp"

namespace runtime {
namespace log_history {

namespace {
char s_ring[CAPACITY];
uint32_t s_total = 0U;

osal::Mutex& lock()
{
    static osal::Mutex s_mutex; // Tạo khi log lần đầu (sau khi RTOS chạy), không phụ thuộc thứ tự khởi tạo tĩnh.
    return s_mutex;
}

uint32_t oldest_locked()
{
    return (s_total > CAPACITY) ? (s_total - static_cast<uint32_t>(CAPACITY)) : 0U;
}
} // namespace

void append(const char* p_text, size_t len)
{
    if ((p_text == nullptr) || (len == 0U)) {
        return;
    }
    if (len > CAPACITY) {
        p_text += len - CAPACITY; // Chỉ phần cuối vừa bộ đệm.
        len = CAPACITY;
    }
    osal::LockGuard guard(lock());
    const size_t start = s_total % CAPACITY;
    const size_t first = ((CAPACITY - start) < len) ? (CAPACITY - start) : len;
    std::memcpy(&s_ring[start], p_text, first);
    std::memcpy(&s_ring[0], &p_text[first], len - first);
    s_total += static_cast<uint32_t>(len);
}

uint32_t total()
{
    osal::LockGuard guard(lock());
    return s_total;
}

size_t read(uint32_t& pos, char* p_out, size_t max_len)
{
    osal::LockGuard guard(lock());
    if (pos < oldest_locked()) {
        pos = oldest_locked();
    }
    const size_t available = s_total - pos;
    const size_t count = (available < max_len) ? available : max_len;
    const size_t start = pos % CAPACITY;
    const size_t first = ((CAPACITY - start) < count) ? (CAPACITY - start) : count;
    std::memcpy(p_out, &s_ring[start], first);
    std::memcpy(&p_out[first], &s_ring[0], count - first);
    pos += static_cast<uint32_t>(count);
    return count;
}

} // namespace log_history
} // namespace runtime
