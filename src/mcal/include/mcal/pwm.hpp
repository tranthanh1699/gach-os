/**
 * @file    pwm.hpp
 * @brief   MCAL contract: kênh PWM (dùng cho backlight, buzzer...).
 */
#ifndef MCAL_PWM_HPP
#define MCAL_PWM_HPP

#include <cstdint>

#include "common/status.hpp"

namespace mcal {

/** @brief Độ phân giải duty chuẩn hóa: 0..1000 (‰), độc lập độ phân giải timer của chip. */
constexpr uint16_t PWM_DUTY_MAX_PERMILLE = 1000U;

/** @brief Một kênh PWM đã cấu hình tần số bởi platform. */
class IPwmChannel {
public:
    virtual ~IPwmChannel() = default;

    /**
     * @brief Đặt duty cycle.
     * @param[in] duty_permille 0..PWM_DUTY_MAX_PERMILLE (giá trị lớn hơn sẽ bị kẹp).
     */
    virtual common::Status set_duty(uint16_t duty_permille) = 0;
};

} // namespace mcal

#endif /* MCAL_PWM_HPP */
