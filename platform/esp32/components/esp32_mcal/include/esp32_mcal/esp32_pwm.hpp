/**
 * @file    esp32_pwm.hpp
 * @brief   MCAL PWM cho họ ESP32 (LEDC low-speed – có trên mọi chip ESP32-xx).
 */
#ifndef ESP32_MCAL_ESP32_PWM_HPP
#define ESP32_MCAL_ESP32_PWM_HPP

#include <cstdint>

#include "common/non_copyable.hpp"
#include "driver/gpio.h"
#include "driver/ledc.h"
#include "mcal/pwm.hpp"

namespace esp32_mcal {

/** @brief Cấu hình kênh LEDC. */
struct PwmConfig {
    gpio_num_t pin;
    ledc_timer_t timer;
    ledc_channel_t channel;
    uint32_t freq_hz;
    ledc_timer_bit_t resolution;
};

/** @brief Kênh PWM, hiện thực mcal::IPwmChannel. */
class Esp32PwmChannel final : public mcal::IPwmChannel, private common::NonCopyable {
public:
    Esp32PwmChannel() : m_cfg{}, m_is_initialized(false) {}

    /** @brief Cấu hình timer + kênh, duty ban đầu = 0. */
    common::Status init(const PwmConfig& cfg);

    common::Status set_duty(uint16_t duty_permille) override;

private:
    PwmConfig m_cfg;
    bool m_is_initialized;
};

} // namespace esp32_mcal

#endif /* ESP32_MCAL_ESP32_PWM_HPP */
