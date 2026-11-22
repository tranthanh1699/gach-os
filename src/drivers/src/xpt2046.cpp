/**
 * @file    xpt2046.cpp
 * @brief   Hiện thực driver XPT2046.
 */
#include "drivers/xpt2046.hpp"

#include <algorithm>
#include <utility>

namespace drivers {

namespace {

// Control byte: S=1 | A2..A0 | MODE=0 (12-bit) | SER/DFR=0 (differential) | PD=00 (IRQ bật giữa các lần đo).
constexpr uint8_t CMD_READ_X = 0xD0U;
constexpr uint8_t CMD_READ_Y = 0x90U;
constexpr uint8_t CMD_READ_Z1 = 0xB0U;
constexpr uint8_t CMD_READ_Z2 = 0xC0U;

constexpr uint16_t ADC_MAX = 4095U;
constexpr uint8_t SAMPLE_COUNT = 5U; // Lấy 5 mẫu, bỏ min/max, trung bình 3 mẫu giữa.

} // namespace

Xpt2046::Xpt2046(mcal::ISpiDevice& spi, mcal::IGpioInput* p_irq, const Xpt2046Config& cfg)
    : m_spi(spi), m_p_irq(p_irq), m_cfg(cfg)
{
}

common::Status Xpt2046::init()
{
    if ((m_cfg.raw_x_max <= m_cfg.raw_x_min) || (m_cfg.raw_y_max <= m_cfg.raw_y_min) || (m_cfg.width == 0U) ||
        (m_cfg.height == 0U)) {
        return common::Status::ERR_PARAM;
    }
    // Một lần đọc giả để đưa chip về chế độ power-down có IRQ.
    uint16_t dummy = 0U;
    return read_channel(CMD_READ_X, dummy);
}

common::Status Xpt2046::read(hal::TouchPoint& point)
{
    point.pressed = false;

    if ((m_p_irq != nullptr) && (m_p_irq->read() == mcal::PinLevel::HIGH)) {
        return common::Status::OK; // PENIRQ không kích hoạt -> không chạm, tiết kiệm SPI.
    }

    uint16_t z1 = 0U;
    uint16_t z2 = 0U;
    common::Status status = read_channel(CMD_READ_Z1, z1);
    if (common::is_ok(status)) {
        status = read_channel(CMD_READ_Z2, z2);
    }
    if (!common::is_ok(status)) {
        return status;
    }
    const int32_t pressure = static_cast<int32_t>(z1) + static_cast<int32_t>(ADC_MAX) - static_cast<int32_t>(z2);
    if (pressure < static_cast<int32_t>(m_cfg.z_threshold)) {
        return common::Status::OK;
    }

    uint16_t raw_x = 0U;
    uint16_t raw_y = 0U;
    status = read_filtered(CMD_READ_X, raw_x);
    if (common::is_ok(status)) {
        status = read_filtered(CMD_READ_Y, raw_y);
    }
    if (!common::is_ok(status)) {
        return status;
    }

    if (m_cfg.swap_xy) {
        std::swap(raw_x, raw_y);
    }
    point.x = map_axis(raw_x, m_cfg.raw_x_min, m_cfg.raw_x_max, m_cfg.width, m_cfg.invert_x);
    point.y = map_axis(raw_y, m_cfg.raw_y_min, m_cfg.raw_y_max, m_cfg.height, m_cfg.invert_y);
    point.pressed = true;
    return common::Status::OK;
}

common::Status Xpt2046::read_channel(uint8_t cmd, uint16_t& value)
{
    const uint8_t tx[3] = {cmd, 0x00U, 0x00U};
    uint8_t rx[3] = {0U, 0U, 0U};
    const common::Status status = m_spi.transfer(tx, rx, sizeof(tx));
    if (common::is_ok(status)) {
        // 12 bit dữ liệu bắt đầu sau 1 clock BUSY: byte1[6:0] + byte2[7:3].
        value = static_cast<uint16_t>(((static_cast<uint16_t>(rx[1]) << 8U) | rx[2]) >> 3U) & ADC_MAX;
    }
    return status;
}

common::Status Xpt2046::read_filtered(uint8_t cmd, uint16_t& value)
{
    uint16_t samples[SAMPLE_COUNT] = {};
    for (uint16_t& sample : samples) {
        const common::Status status = read_channel(cmd, sample);
        if (!common::is_ok(status)) {
            return status;
        }
    }
    std::sort(samples, samples + SAMPLE_COUNT);
    const uint32_t sum = static_cast<uint32_t>(samples[1]) + samples[2] + samples[3];
    value = static_cast<uint16_t>(sum / 3U);
    return common::Status::OK;
}

uint16_t Xpt2046::map_axis(uint16_t raw, uint16_t raw_min, uint16_t raw_max, uint16_t size, bool invert)
{
    const uint16_t clamped = std::min(std::max(raw, raw_min), raw_max);
    const uint32_t span = static_cast<uint32_t>(raw_max) - raw_min;
    uint32_t pos = ((static_cast<uint32_t>(clamped) - raw_min) * (static_cast<uint32_t>(size) - 1U)) / span;
    if (invert) {
        pos = (static_cast<uint32_t>(size) - 1U) - pos;
    }
    return static_cast<uint16_t>(pos);
}

} // namespace drivers
