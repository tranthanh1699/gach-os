/**
 * @file    debug_port.hpp
 * @brief   HAL contract: cổng byte hai chiều tới máy tính phát triển (debug bridge kiểu adb).
 *
 * Thường là UART console (cùng cáp USB dùng để nạp firmware). Cổng có thể bị chia sẻ với log thô của
 * hệ thống (bootloader, printf) -> giao thức phía trên tự đóng khung để phân biệt.
 */
#ifndef HAL_API_DEBUG_PORT_HPP
#define HAL_API_DEBUG_PORT_HPP

#include <cstddef>
#include <cstdint>

#include "common/status.hpp"

namespace hal {

/** @brief Cổng debug. read() chỉ gọi từ một task; write() an toàn đa task. */
class IDebugPort {
public:
    virtual ~IDebugPort() = default;

    /** @brief Khởi tạo cổng (cài driver nhận dữ liệu). */
    virtual common::Status init() = 0;

    /**
     * @brief Đọc tối đa len byte, chờ tối đa timeout_ms nếu chưa có dữ liệu.
     * @return Số byte đã đọc (0 = hết thời gian chờ).
     */
    virtual size_t read(uint8_t* p_buf, size_t len, uint32_t timeout_ms) = 0;

    /**
     * @brief Ghi toàn bộ dữ liệu. Một lần gọi được ghi liền khối (không bị xen bởi lần ghi khác).
     */
    virtual common::Status write(const uint8_t* p_data, size_t len) = 0;
};

} // namespace hal

#endif /* HAL_API_DEBUG_PORT_HPP */
