/**
 * @file    storage.hpp
 * @brief   HAL contract: lưu trữ key-value bền vững (NVS, EEPROM, flash sector...).
 */
#ifndef HAL_API_STORAGE_HPP
#define HAL_API_STORAGE_HPP

#include <cstddef>
#include <cstdint>

#include "common/status.hpp"

namespace hal {

constexpr size_t STORAGE_KEY_MAX_LEN = 15U; /**< Giới hạn chung (NVS: 15 ký tự). */

/** @brief Kho key-value. Mỗi lần ghi được commit ngay (an toàn khi mất điện). */
class IKeyValueStore {
public:
    virtual ~IKeyValueStore() = default;

    /**
     * @brief Đọc chuỗi.
     * @return OK, ERR_NOT_FOUND (chưa có khóa), ERR_NO_MEM (buffer quá nhỏ), ERR_IO.
     */
    virtual common::Status get_string(const char* p_key, char* p_buf, size_t buf_len) const = 0;

    /** @brief Ghi chuỗi. */
    virtual common::Status set_string(const char* p_key, const char* p_value) = 0;

    /** @brief Đọc số nguyên 32 bit. @return OK hoặc ERR_NOT_FOUND. */
    virtual common::Status get_i32(const char* p_key, int32_t& value) const = 0;

    /** @brief Ghi số nguyên 32 bit. */
    virtual common::Status set_i32(const char* p_key, int32_t value) = 0;

    /** @brief Xóa khóa (không lỗi nếu khóa không tồn tại). */
    virtual common::Status erase(const char* p_key) = 0;
};

} // namespace hal

#endif /* HAL_API_STORAGE_HPP */
