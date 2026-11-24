/**
 * @file    trace.hpp
 * @brief   gach trace: ghi sự kiện hiệu năng (đoạn thời gian, mốc, bộ đếm) để xem timeline trên Perfetto.
 *
 * - Lõi header-only, không phụ thuộc RTOS/SDK; backend (timestamp, core, task, bộ đệm) do runtime cắm vào.
 * - Tắt (mặc định): mỗi macro chỉ là một phép đọc cờ. Bật bằng `gach trace start`.
 * - cat / name PHẢI là chuỗi sống suốt chương trình (literal hoặc tĩnh): bộ đệm chỉ lưu con trỏ.
 * - Không gọi trong ISR.
 *
 * @code
 *   void render() { TRACE_SCOPE("ui", "render"); ... }
 *   TRACE_INSTANT("input", "touch_down", x);
 *   TRACE_COUNTER("mem", "internal_free", bytes);
 * @endcode
 */
#ifndef COMMON_TRACE_HPP
#define COMMON_TRACE_HPP

#include <cstdint>

#include "common/non_copyable.hpp"

namespace common {
namespace trace {

/** @brief Loại bản ghi (ánh xạ thẳng sang Chrome trace: B / E / i / C). */
enum class Phase : uint8_t { BEGIN, END, INSTANT, COUNTER };

/** @brief Backend ghi một bản ghi. */
using SinkFn = void (*)(Phase phase, const char* p_cat, const char* p_name, int32_t value);

namespace detail {
inline SinkFn g_sink = nullptr;
inline volatile bool g_enabled = false;
} // namespace detail

/** @brief Cắm backend (composition root). */
inline void set_sink(SinkFn sink)
{
    detail::g_sink = sink;
}

/** @brief Bật / tắt ghi. */
inline void set_enabled(bool is_enabled)
{
    detail::g_enabled = is_enabled;
}

/** @return true nếu đang ghi. */
inline bool is_enabled()
{
    return detail::g_enabled;
}

/** @brief Ghi một bản ghi nếu đang bật. Ưu tiên dùng macro. */
inline void emit(Phase phase, const char* p_cat, const char* p_name, int32_t value)
{
    const SinkFn sink = detail::g_sink;
    if (detail::g_enabled && (sink != nullptr)) {
        sink(phase, p_cat, p_name, value);
    }
}

/** @brief Đoạn thời gian theo phạm vi (BEGIN khi tạo, END khi hủy). */
class Scope final : private NonCopyable {
public:
    Scope(const char* p_cat, const char* p_name) : m_p_cat(p_cat), m_p_name(p_name)
    {
        emit(Phase::BEGIN, m_p_cat, m_p_name, 0);
    }
    ~Scope() { emit(Phase::END, m_p_cat, m_p_name, 0); }

private:
    const char* m_p_cat;
    const char* m_p_name;
};

} // namespace trace
} // namespace common

// NOLINTBEGIN(cppcoreguidelines-macro-usage): macro để có tên biến duy nhất theo dòng.
#define GACH_TRACE_CAT2(a, b) a##b
#define GACH_TRACE_CAT(a, b) GACH_TRACE_CAT2(a, b)
#define TRACE_SCOPE(cat, name) ::common::trace::Scope GACH_TRACE_CAT(gach_trace_scope_, __LINE__){(cat), (name)}
#define TRACE_BEGIN(cat, name) ::common::trace::emit(::common::trace::Phase::BEGIN, (cat), (name), 0)
#define TRACE_END(cat, name) ::common::trace::emit(::common::trace::Phase::END, (cat), (name), 0)
#define TRACE_INSTANT(cat, name, value) \
    ::common::trace::emit(::common::trace::Phase::INSTANT, (cat), (name), static_cast<int32_t>(value))
#define TRACE_COUNTER(cat, name, value) \
    ::common::trace::emit(::common::trace::Phase::COUNTER, (cat), (name), static_cast<int32_t>(value))
// NOLINTEND(cppcoreguidelines-macro-usage)

#endif /* COMMON_TRACE_HPP */
