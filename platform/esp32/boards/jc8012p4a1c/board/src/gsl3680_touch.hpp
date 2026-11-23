/**
 * @file    gsl3680_touch.hpp
 * @brief   hal::ITouch cho controller cảm ứng điện dung GSL3680 (I2C) qua driver esp_lcd_touch của hãng.
 *
 * Tọa độ driver trả về theo hướng gốc của panel; lớp này xoay sang hệ tọa độ logic của IDisplay.
 */
#ifndef BOARD_GSL3680_TOUCH_HPP
#define BOARD_GSL3680_TOUCH_HPP

#include <cstdint>

#include "common/non_copyable.hpp"
#include "driver/gpio.h"
#include "driver/i2c_master.h"
#include "esp_lcd_touch.h"
#include "hal_api/touch.hpp"
#include "rotation.hpp"

namespace board {

/** @brief Cấu hình GSL3680. */
struct Gsl3680Config {
    i2c_port_num_t i2c_port;
    gpio_num_t sda;
    gpio_num_t scl;
    uint32_t i2c_clock_hz;
    gpio_num_t reset_pin;
    gpio_num_t irq_pin;
    uint16_t panel_width;  /**< Hệ tọa độ gốc của panel (hướng dọc). */
    uint16_t panel_height;
    bool mirror_x;         /**< Lật để khớp hướng gốc panel (áp trước khi xoay). */
    bool mirror_y;
    Rotation rotation;     /**< Phải trùng góc xoay của màn hình. */
};

/** @brief Cảm ứng GSL3680, một điểm. */
class Gsl3680Touch final : public hal::ITouch, private common::NonCopyable {
public:
    explicit Gsl3680Touch(const Gsl3680Config& cfg);

    /** @note Nạp firmware vào GSL3680 qua I2C (vài trăm ms). */
    common::Status init() override;
    common::Status read(hal::TouchPoint& point) override;

private:
    const Gsl3680Config m_cfg;
    i2c_master_bus_handle_t m_p_bus;
    esp_lcd_panel_io_handle_t m_p_io;
    esp_lcd_touch_handle_t m_p_touch;
};

} // namespace board

#endif /* BOARD_GSL3680_TOUCH_HPP */
