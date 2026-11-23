/**
 * @file    gsl3680_touch.cpp
 * @brief   Khởi tạo bus I2C + GSL3680, đọc điểm chạm và xoay sang hệ tọa độ logic.
 *
 * Tham số port từ BSP của hãng: vendor/esp32_p4_function_ev_board (bsp_i2c_init, bsp_touch_new).
 */
#include "gsl3680_touch.hpp"

#include "esp_lcd_panel_io.h"
#include "esp_log.h"

// Header vendor (esp_lcd_touch_gsl3680.h) chứa bảng firmware C không hợp lệ trong C++ (narrowing)
// và thiếu extern "C" -> chỉ khai báo lại đúng hàm factory cần dùng.
extern "C" esp_err_t esp_lcd_touch_new_i2c_gsl3680(esp_lcd_panel_io_handle_t io, const esp_lcd_touch_config_t* config,
                                                   esp_lcd_touch_handle_t* out_touch);

namespace board {

namespace {

constexpr const char* TAG = "gsl3680";
constexpr uint16_t GSL3680_I2C_ADDRESS = 0x40U;
constexpr uint8_t GSL3680_CMD_BITS = 8U;
constexpr size_t GSL3680_CONTROL_PHASE_BYTES = 1U;
constexpr uint8_t I2C_GLITCH_FILTER_CYCLES = 7U;
constexpr uint8_t MAX_POINTS = 1U;

/** @brief Kẹp giá trị vào [0, limit - 1]. */
uint16_t clamp_axis(int32_t value, uint16_t limit)
{
    if (value < 0) {
        return 0U;
    }
    return (value >= static_cast<int32_t>(limit)) ? static_cast<uint16_t>(limit - 1U) : static_cast<uint16_t>(value);
}

bool check(esp_err_t err, const char* p_what)
{
    if (err != ESP_OK) {
        ESP_LOGE(TAG, "%s failed: %s", p_what, esp_err_to_name(err));
        return false;
    }
    return true;
}

} // namespace

Gsl3680Touch::Gsl3680Touch(const Gsl3680Config& cfg) : m_cfg(cfg), m_p_bus(nullptr), m_p_io(nullptr), m_p_touch(nullptr)
{
}

common::Status Gsl3680Touch::init()
{
    if (m_p_touch != nullptr) {
        return common::Status::ERR_STATE;
    }

    i2c_master_bus_config_t bus_cfg{};
    bus_cfg.i2c_port = m_cfg.i2c_port;
    bus_cfg.sda_io_num = m_cfg.sda;
    bus_cfg.scl_io_num = m_cfg.scl;
    bus_cfg.clk_source = I2C_CLK_SRC_DEFAULT;
    bus_cfg.glitch_ignore_cnt = I2C_GLITCH_FILTER_CYCLES;
    bus_cfg.flags.enable_internal_pullup = 1U;
    if (!check(i2c_new_master_bus(&bus_cfg, &m_p_bus), "I2C bus")) {
        return common::Status::ERR_IO;
    }

    esp_lcd_panel_io_i2c_config_t io_cfg{};
    io_cfg.dev_addr = GSL3680_I2C_ADDRESS;
    io_cfg.control_phase_bytes = GSL3680_CONTROL_PHASE_BYTES;
    io_cfg.dc_bit_offset = 0U;
    io_cfg.lcd_cmd_bits = GSL3680_CMD_BITS;
    io_cfg.flags.disable_control_phase = 1U;
    io_cfg.scl_speed_hz = m_cfg.i2c_clock_hz;
    if (!check(esp_lcd_new_panel_io_i2c(m_p_bus, &io_cfg, &m_p_io), "touch IO")) {
        return common::Status::ERR_IO;
    }

    esp_lcd_touch_config_t tp_cfg{};
    tp_cfg.x_max = m_cfg.panel_width;
    tp_cfg.y_max = m_cfg.panel_height;
    tp_cfg.rst_gpio_num = m_cfg.reset_pin;
    tp_cfg.int_gpio_num = m_cfg.irq_pin;
    tp_cfg.levels.reset = 0U;
    tp_cfg.levels.interrupt = 0U;
    tp_cfg.flags.swap_xy = 0U;
    tp_cfg.flags.mirror_x = m_cfg.mirror_x ? 1U : 0U;
    tp_cfg.flags.mirror_y = m_cfg.mirror_y ? 1U : 0U;
    if (!check(esp_lcd_touch_new_i2c_gsl3680(m_p_io, &tp_cfg, &m_p_touch), "GSL3680")) {
        m_p_touch = nullptr;
        return common::Status::ERR_IO;
    }
    return common::Status::OK;
}

common::Status Gsl3680Touch::read(hal::TouchPoint& point)
{
    point = hal::TouchPoint{0U, 0U, false};
    if (m_p_touch == nullptr) {
        return common::Status::ERR_STATE;
    }
    if (esp_lcd_touch_read_data(m_p_touch) != ESP_OK) {
        return common::Status::ERR_IO;
    }

    uint16_t raw_x = 0U;
    uint16_t raw_y = 0U;
    uint16_t strength = 0U;
    uint8_t count = 0U;
    // get_coordinates đã áp mirror_x/mirror_y -> tọa độ theo hướng gốc panel.
    if (!esp_lcd_touch_get_coordinates(m_p_touch, &raw_x, &raw_y, &strength, &count, MAX_POINTS) || (count == 0U)) {
        return common::Status::OK;
    }

    const bool swaps = rotation::swaps_axes(m_cfg.rotation);
    const uint16_t log_w = swaps ? m_cfg.panel_height : m_cfg.panel_width;
    const uint16_t log_h = swaps ? m_cfg.panel_width : m_cfg.panel_height;
    const Point logical = rotation::physical_to_logical(
        m_cfg.rotation, Point{static_cast<int32_t>(raw_x), static_cast<int32_t>(raw_y)}, static_cast<int32_t>(log_w),
        static_cast<int32_t>(log_h));

    point.x = clamp_axis(logical.x, log_w);
    point.y = clamp_axis(logical.y, log_h);
    point.pressed = true;
    return common::Status::OK;
}

} // namespace board
