/**
 * @file    status.hpp
 * @brief   Mã trạng thái dùng chung cho mọi tầng (Foundation layer).
 *
 * @note    Tầng `common` không phụ thuộc bất kỳ tầng nào khác; mọi tầng đều được phép include.
 */
#ifndef COMMON_STATUS_HPP
#define COMMON_STATUS_HPP

#include <cstdint>

namespace common {

/** @brief Kết quả trả về của các API có thể thất bại (không dùng exception). */
enum class Status : uint8_t {
    OK = 0U,           /**< Thành công. */
    ERR_PARAM,         /**< Tham số không hợp lệ. */
    ERR_STATE,         /**< Gọi sai trạng thái (chưa init, đã init...). */
    ERR_TIMEOUT,       /**< Quá thời gian chờ. */
    ERR_IO,            /**< Lỗi truyền nhận phần cứng. */
    ERR_NO_MEM,        /**< Hết tài nguyên tĩnh / bộ nhớ. */
    ERR_NOT_SUPPORTED, /**< Tính năng không được nền tảng hỗ trợ. */
    ERR_FULL,          /**< Bảng/registry đã đầy. */
    ERR_NOT_FOUND      /**< Không tìm thấy khóa/đối tượng. */
};

/** @brief Tiện ích kiểm tra nhanh trạng thái thành công. */
constexpr bool is_ok(Status status) { return status == Status::OK; }

} // namespace common

#endif /* COMMON_STATUS_HPP */
