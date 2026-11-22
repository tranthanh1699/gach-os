/**
 * @file    soft_spi.cpp
 * @brief   Hiện thực SPI mềm mode 0.
 */
#include "drivers/soft_spi.hpp"

namespace drivers {

namespace {
constexpr uint8_t MSB_MASK = 0x80U;
constexpr uint8_t BITS_PER_BYTE = 8U;
} // namespace

SoftSpiDevice::SoftSpiDevice(mcal::IGpioOutput& sclk, mcal::IGpioOutput& mosi, mcal::IGpioInput& miso,
                             mcal::IGpioOutput& cs)
    : m_sclk(sclk), m_mosi(mosi), m_miso(miso), m_cs(cs)
{
}

common::Status SoftSpiDevice::transfer(const uint8_t* p_tx, uint8_t* p_rx, size_t len)
{
    if ((p_tx == nullptr) || (len == 0U)) {
        return common::Status::ERR_PARAM;
    }
    m_sclk.write(mcal::PinLevel::LOW);
    m_cs.write(mcal::PinLevel::LOW);
    for (size_t index = 0U; index < len; ++index) {
        const uint8_t in = transfer_byte(p_tx[index]);
        if (p_rx != nullptr) {
            p_rx[index] = in;
        }
    }
    m_cs.write(mcal::PinLevel::HIGH);
    return common::Status::OK;
}

uint8_t SoftSpiDevice::transfer_byte(uint8_t out)
{
    uint8_t in = 0U;
    for (uint8_t bit = 0U; bit < BITS_PER_BYTE; ++bit) {
        // Mode 0: đặt dữ liệu khi SCLK thấp, lấy mẫu ở cạnh lên.
        m_mosi.write(((out & MSB_MASK) != 0U) ? mcal::PinLevel::HIGH : mcal::PinLevel::LOW);
        out = static_cast<uint8_t>(out << 1U);
        m_sclk.write(mcal::PinLevel::HIGH);
        in = static_cast<uint8_t>((in << 1U) | ((m_miso.read() == mcal::PinLevel::HIGH) ? 1U : 0U));
        m_sclk.write(mcal::PinLevel::LOW);
    }
    return in;
}

} // namespace drivers
