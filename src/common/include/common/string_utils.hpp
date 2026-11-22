/**
 * @file    string_utils.hpp
 * @brief   Tiện ích chuỗi an toàn, không cấp phát động.
 */
#ifndef COMMON_STRING_UTILS_HPP
#define COMMON_STRING_UTILS_HPP

#include <cstddef>

namespace common {

/**
 * @brief Sao chép chuỗi có giới hạn, luôn kết thúc bằng '\0' (thay cho strcpy/strncpy).
 *
 * @param[out] p_dst    Bộ đệm đích.
 * @param[in]  dst_size Kích thước bộ đệm đích (byte, gồm cả '\0').
 * @param[in]  p_src    Chuỗi nguồn (NULL được coi là chuỗi rỗng).
 * @return Số ký tự đã chép (không gồm '\0'); bị cắt nếu nguồn dài hơn dst_size - 1.
 */
inline size_t copy_string(char* p_dst, size_t dst_size, const char* p_src)
{
    if ((p_dst == nullptr) || (dst_size == 0U)) {
        return 0U;
    }
    size_t count = 0U;
    if (p_src != nullptr) {
        while (((count + 1U) < dst_size) && (p_src[count] != '\0')) {
            p_dst[count] = p_src[count];
            ++count;
        }
    }
    p_dst[count] = '\0';
    return count;
}

} // namespace common

#endif /* COMMON_STRING_UTILS_HPP */
