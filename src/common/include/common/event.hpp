/**
 * @file    event.hpp
 * @brief   Sổ sự kiện hệ thống (luôn bật): mở / đóng app, mạng, thời gian, cài app, lỗi script...
 *
 * Mỗi sự kiện vừa vào sổ sự kiện (backend do runtime cắm: `gach event list | monitor`) vừa thành một dòng log cùng
 * mức – dùng EVENT_x thay cho LOGx ở các mốc quan trọng. Header-only, không phụ thuộc RTOS/SDK. Không gọi từ ISR.
 *
 * @code
 *   EVENT_I("wifi", "connected to '%s', ip %s", ssid, ip);
 *   EVENT_E("script", "%s: %s", app_id, message);
 * @endcode
 */
#ifndef COMMON_EVENT_HPP
#define COMMON_EVENT_HPP

#include <cstdarg>
#include <cstddef>
#include <cstdio>

#include "common/log.hpp"

namespace common {
namespace event {

/** @brief Mức độ của sự kiện. */
enum class Severity : uint8_t { INFO, WARN, ERROR };

constexpr size_t TEXT_MAX_LEN = 96U; /**< Nội dung tối đa (phần dư bị cắt). */

/** @brief Backend: p_source sống suốt chương trình (literal); p_text chỉ hợp lệ trong lời gọi. */
using SinkFn = void (*)(Severity severity, const char* p_source, const char* p_text);

namespace detail {
inline SinkFn g_sink = nullptr;
} // namespace detail

/** @brief Cắm backend (composition root). */
inline void set_sink(SinkFn sink)
{
    detail::g_sink = sink;
}

/** @brief Ghi một sự kiện (sổ sự kiện + log). Ưu tiên dùng macro. */
__attribute__((format(printf, 3, 4))) inline void write(Severity severity, const char* p_source, const char* p_fmt, ...)
{
    char text[TEXT_MAX_LEN + 1U];
    va_list args;
    va_start(args, p_fmt);
    (void)std::vsnprintf(text, sizeof(text), p_fmt, args);
    va_end(args);
    const SinkFn sink = detail::g_sink;
    if (sink != nullptr) {
        sink(severity, p_source, text);
    }
    const log::Level level = (severity == Severity::ERROR)  ? log::Level::ERROR
                             : (severity == Severity::WARN) ? log::Level::WARN
                                                            : log::Level::INFO;
    log::write(level, p_source, "%s", text);
}

} // namespace event
} // namespace common

// NOLINTBEGIN(cppcoreguidelines-macro-usage)
#define EVENT_I(source, ...) ::common::event::write(::common::event::Severity::INFO, (source), __VA_ARGS__)
#define EVENT_W(source, ...) ::common::event::write(::common::event::Severity::WARN, (source), __VA_ARGS__)
#define EVENT_E(source, ...) ::common::event::write(::common::event::Severity::ERROR, (source), __VA_ARGS__)
// NOLINTEND(cppcoreguidelines-macro-usage)

#endif /* COMMON_EVENT_HPP */
