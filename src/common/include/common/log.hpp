/**
 * @file    log.hpp
 * @brief   gach log: bộ ghi log chung cho mọi tầng của OS (common -> apps).
 *
 * - Lõi KHÔNG phụ thuộc RTOS/SDK: chỉ lọc mức log, format chuỗi vào buffer trên stack rồi giao cho backend.
 * - Backend (đầu ra, timestamp, lịch sử) do composition root của platform cắm vào bằng set_backend();
 *   chưa có backend thì log bị bỏ qua (an toàn khi gọi rất sớm lúc khởi động).
 * - Mức log: toàn cục + ghi đè theo tag (tối đa MAX_TAG_LEVELS tag), đổi được lúc chạy.
 * - Mức tối thiểu lúc biên dịch: định nghĩa GACH_LOG_MIN_LEVEL (0..5) để loại hẳn DEBUG/VERBOSE khỏi bản release.
 *
 * @code
 *   constexpr const char* TAG = "wifi";
 *   LOGI(TAG, "connected to %s", ssid);
 *   LOGE(TAG, "connect failed (status=%u)", static_cast<unsigned>(status));
 *   common::log::set_level("wifi", common::log::Level::VERBOSE);
 * @endcode
 *
 * @note Không gọi từ ISR. Mỗi lời gọi gửi một dòng trọn vẹn cho backend -> log từ nhiều task không xen nhau
 *       (nếu backend ghi nguyên khối). Đổi mức log theo tag trong lúc task khác đang log là an toàn ở mức "có thể
 *       lọc sai một dòng", không làm hỏng bộ nhớ.
 */
#ifndef COMMON_LOG_HPP
#define COMMON_LOG_HPP

#include <cstdarg>
#include <cstddef>
#include <cstdint>
#include <cstdio>
#include <cstring>

namespace common {
namespace log {

/** @brief Mức log, tăng dần độ chi tiết. */
enum class Level : uint8_t {
    NONE = 0U, /**< Tắt. */
    ERROR,     /**< Lỗi: chức năng hỏng. */
    WARN,      /**< Bất thường nhưng vẫn chạy tiếp. */
    INFO,      /**< Sự kiện quan trọng của hệ thống. */
    DEBUG,     /**< Chi tiết cho người phát triển. */
    VERBOSE    /**< Rất chi tiết (vòng lặp, dữ liệu). */
};

constexpr size_t MESSAGE_MAX_LEN = 200U; /**< Độ dài nội dung tối đa một dòng (phần dư bị cắt). */
constexpr size_t TAG_MAX_LEN = 15U;      /**< Tag dài hơn chỉ so khớp 15 ký tự đầu khi lọc theo tag. */
constexpr size_t MAX_TAG_LEVELS = 16U;   /**< Số tag được ghi đè mức log. */
constexpr Level DEFAULT_LEVEL = Level::INFO;

/**
 * @brief Đầu ra của log (do platform cung cấp).
 * @param level  Mức của dòng.
 * @param p_tag  Tag (không NULL).
 * @param p_text Nội dung đã format (không có '\n' cuối).
 * @param len    Độ dài nội dung.
 */
using WriteFn = void (*)(Level level, const char* p_tag, const char* p_text, size_t len);

/** @brief Một tag được ghi đè mức log. */
struct TagLevel {
    char tag[TAG_MAX_LEN + 1U];
    Level level;
};

namespace detail {
inline WriteFn g_backend = nullptr;
inline Level g_level = DEFAULT_LEVEL;
inline TagLevel g_tags[MAX_TAG_LEVELS] = {};
inline size_t g_tag_count = 0U;

inline bool tag_equals(const char* p_a, const char* p_b)
{
    return std::strncmp(p_a, p_b, TAG_MAX_LEN) == 0;
}
} // namespace detail

/** @brief Cắm backend (gọi một lần ở composition root, trước khi các tầng bắt đầu log). */
inline void set_backend(WriteFn backend)
{
    detail::g_backend = backend;
}

/** @brief Đặt mức log toàn cục (tag không được ghi đè sẽ dùng mức này). */
inline void set_level(Level level)
{
    detail::g_level = level;
}

/** @return Mức log toàn cục. */
inline Level global_level()
{
    return detail::g_level;
}

/**
 * @brief Ghi đè mức log cho một tag.
 * @return false nếu bảng ghi đè đã đầy (MAX_TAG_LEVELS) hoặc tag rỗng.
 */
inline bool set_level(const char* p_tag, Level level)
{
    if ((p_tag == nullptr) || (p_tag[0] == '\0')) {
        return false;
    }
    for (size_t index = 0U; index < detail::g_tag_count; ++index) {
        if (detail::tag_equals(detail::g_tags[index].tag, p_tag)) {
            detail::g_tags[index].level = level;
            return true;
        }
    }
    if (detail::g_tag_count >= MAX_TAG_LEVELS) {
        return false;
    }
    TagLevel& entry = detail::g_tags[detail::g_tag_count];
    (void)std::snprintf(entry.tag, sizeof(entry.tag), "%s", p_tag);
    entry.level = level;
    ++detail::g_tag_count; // Tăng sau khi điền xong: task khác chỉ thấy mục đã hoàn chỉnh.
    return true;
}

/** @brief Xóa mọi ghi đè theo tag. */
inline void clear_tag_levels()
{
    detail::g_tag_count = 0U;
}

/** @return Số tag đang được ghi đè; dùng tag_level_at() để đọc. */
inline size_t tag_level_count()
{
    return detail::g_tag_count;
}

/** @return Mục ghi đè thứ index (index < tag_level_count()). */
inline const TagLevel& tag_level_at(size_t index)
{
    return detail::g_tags[index];
}

/** @return Mức log hiệu lực của tag. */
inline Level level_of(const char* p_tag)
{
    if (p_tag != nullptr) {
        const size_t count = detail::g_tag_count;
        for (size_t index = 0U; index < count; ++index) {
            if (detail::tag_equals(detail::g_tags[index].tag, p_tag)) {
                return detail::g_tags[index].level;
            }
        }
    }
    return detail::g_level;
}

/** @return true nếu một dòng mức level của tag sẽ được ghi. */
inline bool is_enabled(Level level, const char* p_tag)
{
    return (detail::g_backend != nullptr) && (level != Level::NONE) &&
           (static_cast<uint8_t>(level) <= static_cast<uint8_t>(level_of(p_tag)));
}

/** @return Ký tự đại diện mức: E W I D V (N cho NONE). */
inline char level_char(Level level)
{
    static constexpr char CHARS[] = {'N', 'E', 'W', 'I', 'D', 'V'};
    const auto index = static_cast<size_t>(level);
    return (index < sizeof(CHARS)) ? CHARS[index] : '?';
}

/**
 * @brief Đọc mức từ chữ (đúng tên, không phân biệt hoa thường): "n"/"none", "e"/"error", "w"/"warn",
 *        "i"/"info", "d"/"debug", "v"/"verbose".
 * @return false nếu không nhận ra (VD "wifi" KHÔNG phải "w").
 */
inline bool parse_level(const char* p_text, Level& out)
{
    static const struct {
        const char* p_short;
        const char* p_long;
        Level level;
    } NAMES[] = {{"n", "none", Level::NONE},  {"e", "error", Level::ERROR}, {"w", "warn", Level::WARN},
                 {"i", "info", Level::INFO},  {"d", "debug", Level::DEBUG}, {"v", "verbose", Level::VERBOSE}};
    if (p_text == nullptr) {
        return false;
    }
    char lower[sizeof("verbose")] = {};
    size_t len = 0U;
    for (; (p_text[len] != '\0') && (len < (sizeof(lower) - 1U)); ++len) {
        const char c = p_text[len];
        lower[len] = ((c >= 'A') && (c <= 'Z')) ? static_cast<char>(c - 'A' + 'a') : c;
    }
    if (p_text[len] != '\0') {
        return false; // Dài hơn mọi tên mức.
    }
    for (const auto& name : NAMES) {
        if ((std::strcmp(lower, name.p_short) == 0) || (std::strcmp(lower, name.p_long) == 0)) {
            out = name.level;
            return true;
        }
    }
    return false;
}

/** @brief Ghi một dòng (va_list). Ưu tiên dùng macro LOGx. */
inline void vwrite(Level level, const char* p_tag, const char* p_fmt, va_list args)
{
    const char* const p_safe_tag = (p_tag != nullptr) ? p_tag : "?";
    if (!is_enabled(level, p_safe_tag) || (p_fmt == nullptr)) {
        return;
    }
    char text[MESSAGE_MAX_LEN + 1U];
    const int written = std::vsnprintf(text, sizeof(text), p_fmt, args);
    if (written < 0) {
        return;
    }
    size_t len = (static_cast<size_t>(written) < sizeof(text)) ? static_cast<size_t>(written) : (sizeof(text) - 1U);
    while ((len > 0U) && ((text[len - 1U] == '\n') || (text[len - 1U] == '\r'))) {
        --len; // Backend tự xuống dòng.
    }
    text[len] = '\0';
    const WriteFn backend = detail::g_backend;
    if (backend != nullptr) {
        backend(level, p_safe_tag, text, len);
    }
}

/** @brief Ghi một dòng kiểu printf. Ưu tiên dùng macro LOGx. */
__attribute__((format(printf, 3, 4))) inline void write(Level level, const char* p_tag, const char* p_fmt, ...)
{
    va_list args;
    va_start(args, p_fmt);
    vwrite(level, p_tag, p_fmt, args);
    va_end(args);
}

} // namespace log
} // namespace common

/** Mức tối thiểu lúc biên dịch (0 NONE .. 5 VERBOSE): dòng chi tiết hơn bị loại khỏi binary. */
#ifndef GACH_LOG_MIN_LEVEL
#define GACH_LOG_MIN_LEVEL 5
#endif

// NOLINTBEGIN(cppcoreguidelines-macro-usage): macro để loại bỏ lúc biên dịch và giữ kiểm tra format printf.
#define GACH_LOG_AT(level_num, level, tag, ...)                                                                        \
    do {                                                                                                               \
        if ((level_num) <= GACH_LOG_MIN_LEVEL) {                                                                       \
            ::common::log::write((level), (tag), __VA_ARGS__);                                                         \
        }                                                                                                              \
    } while (0)

#define LOGE(tag, ...) GACH_LOG_AT(1, ::common::log::Level::ERROR, tag, __VA_ARGS__)
#define LOGW(tag, ...) GACH_LOG_AT(2, ::common::log::Level::WARN, tag, __VA_ARGS__)
#define LOGI(tag, ...) GACH_LOG_AT(3, ::common::log::Level::INFO, tag, __VA_ARGS__)
#define LOGD(tag, ...) GACH_LOG_AT(4, ::common::log::Level::DEBUG, tag, __VA_ARGS__)
#define LOGV(tag, ...) GACH_LOG_AT(5, ::common::log::Level::VERBOSE, tag, __VA_ARGS__)
// NOLINTEND(cppcoreguidelines-macro-usage)

#endif /* COMMON_LOG_HPP */
