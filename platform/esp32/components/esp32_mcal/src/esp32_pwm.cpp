/**
 * @file    esp32_pwm.cpp
 * @brief   Hiện thực MCAL PWM (LEDC).
 */
#include "esp32_mcal/esp32_pwm.hpp"

namespace esp32_mcal {

common::Status Esp32PwmChannel::init(const PwmConfig& cfg)
{
    if (m_is_initialized) {
        return common::Status::ERR_STATE;
    }
    ledc_timer_config_t timer_cfg{};
    timer_cfg.speed_mode = LEDC_LOW_SPEED_MODE;
    timer_cfg.duty_resolution = cfg.resolution;
    timer_cfg.timer_num = cfg.timer;
    timer_cfg.freq_hz = cfg.freq_hz;
    timer_cfg.clk_cfg = LEDC_AUTO_CLK;
    if (ledc_timer_config(&timer_cfg) != ESP_OK) {
        return common::Status::ERR_IO;
    }

    ledc_channel_config_t ch_cfg{};
    ch_cfg.gpio_num = cfg.pin;
    ch_cfg.speed_mode = LEDC_LOW_SPEED_MODE;
    ch_cfg.channel = cfg.channel;
    ch_cfg.intr_type = LEDC_INTR_DISABLE;
    ch_cfg.timer_sel = cfg.timer;
    ch_cfg.duty = 0U;
    ch_cfg.hpoint = 0;
    if (ledc_channel_config(&ch_cfg) != ESP_OK) {
        return common::Status::ERR_IO;
    }
    m_cfg = cfg;
    m_is_initialized = true;
    return common::Status::OK;
}

common::Status Esp32PwmChannel::set_duty(uint16_t duty_permille)
{
    if (!m_is_initialized) {
        return common::Status::ERR_STATE;
    }
    const uint32_t permille =
        (duty_permille > mcal::PWM_DUTY_MAX_PERMILLE) ? mcal::PWM_DUTY_MAX_PERMILLE : duty_permille;
    const uint32_t max_duty = (1UL << static_cast<uint32_t>(m_cfg.resolution)) - 1UL;
    const uint32_t duty = (permille * max_duty) / mcal::PWM_DUTY_MAX_PERMILLE;

    if ((ledc_set_duty(LEDC_LOW_SPEED_MODE, m_cfg.channel, duty) != ESP_OK) ||
        (ledc_update_duty(LEDC_LOW_SPEED_MODE, m_cfg.channel) != ESP_OK)) {
        return common::Status::ERR_IO;
    }
    return common::Status::OK;
}

} // namespace esp32_mcal
