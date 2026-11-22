/**
 * @file    xpt2046.hpp
 * @brief   Driver cảm ứng điện trở XPT2046/ADS7843 qua SPI (portable).
 */
#ifndef DRIVERS_XPT2046_HPP
#define DRIVERS_XPT2046_HPP

#include <cstdint>

#include "common/non_copyable.hpp"
#include "hal_api/touch.hpp"
#include "mcal/gpio.hpp"
#include "mcal/spi.hpp"

namespace drivers {

/**
 * @brief Cấu hình hiệu chuẩn.
 *
 * Thứ tự xử lý: đọc raw -> (swap_xy) -> map [raw_min, raw_max] -> [0, size) -> (invert).
 * Dải raw_x_* áp cho trục cho ra tọa độ X màn hình (sau khi swap).
 */
struct Xpt2046Config {
    uint16_t width;       /**< Chiều rộng màn hình (pixel). */
    uint16_t height;      /**< Chiều cao màn hình (pixel). */
    uint16_t raw_x_min;   /**< ADC tại mép trái. */
    uint16_t raw_x_max;   /**< ADC tại mép phải. */
    uint16_t raw_y_min;   /**< ADC tại mép trên. */
    uint16_t raw_y_max;   /**< ADC tại mép dưới. */
    bool swap_xy;         /**< Hoán đổi trục raw. */
    bool invert_x;        /**< Đảo trục X sau khi map. */
    bool invert_y;        /**< Đảo trục Y sau khi map. */
    uint16_t z_threshold; /**< Ngưỡng áp lực tối thiểu để coi là đang chạm. */
};

/** @brief Controller XPT2046, hiện thực hal::ITouch. */
class Xpt2046 final : public hal::ITouch, private common::NonCopyable {
public:
    /**
     * @param[in] spi   Thiết bị SPI (clock <= 2 MHz).
     * @param[in] p_irq Chân PENIRQ (active-low), NULL nếu không nối – khi đó dùng đo áp lực Z.
     * @param[in] cfg   Cấu hình hiệu chuẩn.
     */
    Xpt2046(mcal::ISpiDevice& spi, mcal::IGpioInput* p_irq, const Xpt2046Config& cfg);

    common::Status init() override;
    common::Status read(hal::TouchPoint& point) override;

private:
    common::Status read_channel(uint8_t cmd, uint16_t& value);
    common::Status read_filtered(uint8_t cmd, uint16_t& value);
    static uint16_t map_axis(uint16_t raw, uint16_t raw_min, uint16_t raw_max, uint16_t size, bool invert);

    mcal::ISpiDevice& m_spi;
    mcal::IGpioInput* m_p_irq;
    Xpt2046Config m_cfg;
};

} // namespace drivers

#endif /* DRIVERS_XPT2046_HPP */
