/**
 * @file    spi.hpp
 * @brief   MCAL contract: thiết bị trên bus SPI (CS do platform quản lý).
 */
#ifndef MCAL_SPI_HPP
#define MCAL_SPI_HPP

#include <cstddef>
#include <cstdint>

#include "common/status.hpp"

namespace mcal {

/**
 * @brief Một thiết bị SPI (một chân CS) đã được gắn vào bus bởi platform.
 *
 * @note  Mỗi lời gọi là một transaction trọn vẹn: CS active -> truyền -> CS inactive.
 * @warning Buffer truyền qua DMA phải nằm trong RAM DMA-capable, căn lề 4 byte.
 */
class ISpiDevice {
public:
    virtual ~ISpiDevice() = default;

    /**
     * @brief Truyền/nhận full-duplex, blocking cho tới khi hoàn tất.
     *
     * @param[in]  p_tx Dữ liệu gửi (không NULL).
     * @param[out] p_rx Bộ đệm nhận, NULL nếu chỉ ghi.
     * @param[in]  len  Số byte (> 0).
     * @return common::Status::OK hoặc mã lỗi.
     */
    virtual common::Status transfer(const uint8_t* p_tx, uint8_t* p_rx, size_t len) = 0;

    /** @brief Chỉ ghi (half-duplex TX). */
    common::Status write(const uint8_t* p_tx, size_t len) { return transfer(p_tx, nullptr, len); }
};

} // namespace mcal

#endif /* MCAL_SPI_HPP */
