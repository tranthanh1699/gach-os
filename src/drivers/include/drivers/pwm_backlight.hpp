/**
 * @file    pwm_backlight.hpp
 * @brief   Đèn nền điều khiển bằng PWM (portable).
 */
#ifndef DRIVERS_PWM_BACKLIGHT_HPP
#define DRIVERS_PWM_BACKLIGHT_HPP

#include <cstdint>

#include "common/non_copyable.hpp"
#include "hal_api/backlight.hpp"
#include "mcal/pwm.hpp"

namespace drivers {

/** @brief Backlight PWM, hiện thực hal::IBacklight. */
class PwmBacklight final : public hal::IBacklight, private common::NonCopyable {
public:
    /**
     * @param[in] pwm         Kênh PWM đã cấu hình tần số.
     * @param[in] active_high true nếu duty cao = sáng hơn.
     */
    PwmBacklight(mcal::IPwmChannel& pwm, bool active_high);

    common::Status set_brightness(uint8_t percent) override;
    uint8_t brightness() const override { return m_brightness; }

private:
    mcal::IPwmChannel& m_pwm;
    bool m_active_high;
    uint8_t m_brightness;
};

} // namespace drivers

#endif /* DRIVERS_PWM_BACKLIGHT_HPP */
