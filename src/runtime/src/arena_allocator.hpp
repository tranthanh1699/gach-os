/**
 * @file    arena_allocator.hpp
 * @brief   [Private] Bộ cấp phát first-fit + gộp khối trên một vùng nhớ cố định (cho máy ảo Lua).
 *
 * Toàn bộ heap của app script nằm trong vùng này; reset() thu hồi tất cả khi app thoát
 * -> không thể rò rỉ bộ nhớ ra hệ thống dù script lỗi.
 */
#ifndef RUNTIME_ARENA_ALLOCATOR_HPP
#define RUNTIME_ARENA_ALLOCATOR_HPP

#include <cstddef>
#include <cstdint>

#include "common/non_copyable.hpp"

namespace runtime {

/** @brief Heap trên vùng nhớ cố định. Không thread-safe (chỉ UI task dùng). */
class ArenaAllocator : private common::NonCopyable {
public:
    ArenaAllocator() : m_p_base(nullptr), m_size(0U), m_p_free(nullptr), m_used(0U), m_peak(0U) {}

    /** @brief Gắn vùng nhớ và đặt lại thành một khối trống duy nhất. */
    void reset(void* p_base, size_t size);

    /** @return Con trỏ căn lề 8 byte, NULL nếu hết bộ nhớ. */
    void* allocate(size_t size);

    /** @brief Trả khối về arena (NULL được bỏ qua). */
    void release(void* p_mem);

    /** @brief Đổi kích thước khối (ngữ nghĩa realloc). */
    void* reallocate(void* p_mem, size_t size);

    size_t capacity() const { return m_size; }
    size_t used() const { return m_used; }
    size_t peak() const { return m_peak; }

private:
    struct FreeBlock {
        size_t size; /**< Kích thước cả khối, gồm header. */
        FreeBlock* p_next;
    };

    static size_t block_size_of(const void* p_mem);

    uint8_t* m_p_base;
    size_t m_size;
    FreeBlock* m_p_free; /**< Danh sách khối trống, sắp theo địa chỉ tăng dần. */
    size_t m_used;
    size_t m_peak;
};

} // namespace runtime

#endif /* RUNTIME_ARENA_ALLOCATOR_HPP */
