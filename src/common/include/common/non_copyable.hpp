/**
 * @file    non_copyable.hpp
 * @brief   Base class cấm copy/move cho các đối tượng sở hữu tài nguyên phần cứng.
 */
#ifndef COMMON_NON_COPYABLE_HPP
#define COMMON_NON_COPYABLE_HPP

namespace common {

/** @brief Kế thừa private để cấm copy/move (driver, service, singleton). */
class NonCopyable {
public:
    NonCopyable(const NonCopyable&) = delete;
    NonCopyable& operator=(const NonCopyable&) = delete;
    NonCopyable(NonCopyable&&) = delete;
    NonCopyable& operator=(NonCopyable&&) = delete;

protected:
    NonCopyable() = default;
    ~NonCopyable() = default;
};

} // namespace common

#endif /* COMMON_NON_COPYABLE_HPP */
