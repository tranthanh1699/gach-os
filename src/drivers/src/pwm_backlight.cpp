/**
 * @file    pwm_backlight.cpp
 * @brief   Hiện thực PwmBacklight.
 */
#include "drivers/pwm_backlight.hpp"

namespace drivers {

PwmBacklight::PwmBacklight(mcal::IPwmChannel& pwm, bool active_high)
    : m_pwm(pwm), m_active_high(active_high), m_brightness(0U)
{
}

common::Status PwmBacklight::set_brightness(uint8_t percent)
{
    const uint8_t level = (percent > hal::BRIGHTNESS_MAX) ? hal::BRIGHTNESS_MAX : percent;
    uint32_t duty = (static_cast<uint32_t>(level) * mcal::PWM_DUTY_MAX_PERMILLE) / hal::BRIGHTNESS_MAX;
    if (!m_active_high) {
        duty = mcal::PWM_DUTY_MAX_PERMILLE - duty;
    }
    const common::Status status = m_pwm.set_duty(static_cast<uint16_t>(duty));
    if (common::is_ok(status)) {
        m_brightness = level;
    }
    return status;
}

} // namespace drivers
