/**
 * @file    arena_allocator.cpp
 * @brief   Hiện thực bộ cấp phát arena.
 */
#include "arena_allocator.hpp"

#include <cstring>

namespace runtime {

namespace {

constexpr size_t ALIGNMENT = 8U;
constexpr size_t HEADER_SIZE = 8U; // Lưu kích thước khối, giữ payload căn lề 8.
constexpr size_t MIN_BLOCK = 16U;  // Đủ chứa FreeBlock khi khối được trả lại.

constexpr size_t align_up(size_t value)
{
    return (value + (ALIGNMENT - 1U)) & ~(ALIGNMENT - 1U);
}

} // namespace

void ArenaAllocator::reset(void* p_base, size_t size)
{
    // Căn lề địa chỉ gốc và kích thước để mọi khối đều căn lề 8.
    const uintptr_t raw = reinterpret_cast<uintptr_t>(p_base);
    const uintptr_t aligned = (raw + (ALIGNMENT - 1U)) & ~static_cast<uintptr_t>(ALIGNMENT - 1U);
    const size_t lost = static_cast<size_t>(aligned - raw);

    m_used = 0U;
    m_peak = 0U;
    if ((p_base == nullptr) || (size <= (lost + MIN_BLOCK))) {
        m_p_base = nullptr;
        m_size = 0U;
        m_p_free = nullptr;
        return;
    }
    m_p_base = reinterpret_cast<uint8_t*>(aligned);
    m_size = (size - lost) & ~(ALIGNMENT - 1U);
    m_p_free = reinterpret_cast<FreeBlock*>(m_p_base);
    m_p_free->size = m_size;
    m_p_free->p_next = nullptr;
}

void* ArenaAllocator::allocate(size_t size)
{
    if ((size == 0U) || (size > m_size)) {
        return nullptr;
    }
    size_t needed = align_up(size + HEADER_SIZE);
    needed = (needed < MIN_BLOCK) ? MIN_BLOCK : needed;

    FreeBlock** pp_link = &m_p_free;
    while (*pp_link != nullptr) {
        FreeBlock* const p_block = *pp_link;
        if (p_block->size >= needed) {
            if ((p_block->size - needed) >= MIN_BLOCK) {
                // Tách: phần còn lại giữ nguyên vị trí trong danh sách.
                auto* const p_rest = reinterpret_cast<FreeBlock*>(reinterpret_cast<uint8_t*>(p_block) + needed);
                p_rest->size = p_block->size - needed;
                p_rest->p_next = p_block->p_next;
                *pp_link = p_rest;
                p_block->size = needed;
            } else {
                *pp_link = p_block->p_next;
            }
            m_used += p_block->size;
            m_peak = (m_used > m_peak) ? m_used : m_peak;
            return reinterpret_cast<uint8_t*>(p_block) + HEADER_SIZE;
        }
        pp_link = &p_block->p_next;
    }
    return nullptr;
}

void ArenaAllocator::release(void* p_mem)
{
    if (p_mem == nullptr) {
        return;
    }
    auto* const p_block = reinterpret_cast<FreeBlock*>(static_cast<uint8_t*>(p_mem) - HEADER_SIZE);
    m_used -= p_block->size;

    // Chèn theo thứ tự địa chỉ để gộp được với khối liền kề.
    FreeBlock* p_prev = nullptr;
    FreeBlock* p_next = m_p_free;
    while ((p_next != nullptr) && (p_next < p_block)) {
        p_prev = p_next;
        p_next = p_next->p_next;
    }
    p_block->p_next = p_next;
    if ((p_next != nullptr) && ((reinterpret_cast<uint8_t*>(p_block) + p_block->size) == reinterpret_cast<uint8_t*>(p_next))) {
        p_block->size += p_next->size;
        p_block->p_next = p_next->p_next;
    }
    if (p_prev == nullptr) {
        m_p_free = p_block;
    } else if ((reinterpret_cast<uint8_t*>(p_prev) + p_prev->size) == reinterpret_cast<uint8_t*>(p_block)) {
        p_prev->size += p_block->size;
        p_prev->p_next = p_block->p_next;
    } else {
        p_prev->p_next = p_block;
    }
}

void* ArenaAllocator::reallocate(void* p_mem, size_t size)
{
    if (p_mem == nullptr) {
        return allocate(size);
    }
    if (size == 0U) {
        release(p_mem);
        return nullptr;
    }
    const size_t capacity = block_size_of(p_mem) - HEADER_SIZE;
    if (size <= capacity) {
        return p_mem; // Đủ chỗ: giữ nguyên (Lua thường thu nhỏ bảng/chuỗi).
    }
    void* const p_new = allocate(size);
    if (p_new != nullptr) {
        (void)std::memcpy(p_new, p_mem, capacity);
        release(p_mem);
    }
    return p_new;
}

size_t ArenaAllocator::block_size_of(const void* p_mem)
{
    const auto* const p_block = reinterpret_cast<const FreeBlock*>(static_cast<const uint8_t*>(p_mem) - HEADER_SIZE);
    return p_block->size;
}

} // namespace runtime
