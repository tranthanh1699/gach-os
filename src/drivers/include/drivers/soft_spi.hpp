/**
 * @file    soft_spi.hpp
 * @brief   SPI mềm (bit-bang, mode 0, MSB first) trên các chân GPIO của MCAL – portable.
 *
 * Dùng cho thiết bị tốc độ thấp (VD: touch XPT2046) khi bus SPI phần cứng đã được dùng hết.
 */
#ifndef DRIVERS_SOFT_SPI_HPP
#define DRIVERS_SOFT_SPI_HPP

#include <cstddef>
#include <cstdint>

#include "common/non_copyable.hpp"
#include "mcal/gpio.hpp"
#include "mcal/spi.hpp"

namespace drivers {

/** @brief Hiện thực mcal::ISpiDevice bằng GPIO. Tốc độ phụ thuộc tốc độ ghi GPIO của chip (~vài trăm kHz). */
class SoftSpiDevice final : public mcal::ISpiDevice, private common::NonCopyable {
public:
    /**
     * @param[in] sclk  Chân clock (output, mức nghỉ LOW).
     * @param[in] mosi  Chân dữ liệu ra.
     * @param[in] miso  Chân dữ liệu vào.
     * @param[in] cs    Chân chip-select (active LOW).
     */
    SoftSpiDevice(mcal::IGpioOutput& sclk, mcal::IGpioOutput& mosi, mcal::IGpioInput& miso, mcal::IGpioOutput& cs);

    common::Status transfer(const uint8_t* p_tx, uint8_t* p_rx, size_t len) override;

private:
    uint8_t transfer_byte(uint8_t out);

    mcal::IGpioOutput& m_sclk;
    mcal::IGpioOutput& m_mosi;
    mcal::IGpioInput& m_miso;
    mcal::IGpioOutput& m_cs;
};

} // namespace drivers

#endif /* DRIVERS_SOFT_SPI_HPP */
