/**
 * @file    backlight.hpp
 * @brief   HAL contract: đèn nền màn hình.
 */
#ifndef HAL_API_BACKLIGHT_HPP
#define HAL_API_BACKLIGHT_HPP

#include <cstdint>

#include "common/status.hpp"

namespace hal {

/** @brief Độ sáng tối đa (phần trăm). */
constexpr uint8_t BRIGHTNESS_MAX = 100U;

/** @brief Đèn nền (PWM hoặc on/off). */
class IBacklight {
public:
    virtual ~IBacklight() = default;

    /**
     * @brief Đặt độ sáng.
     * @param[in] percent 0..BRIGHTNESS_MAX (giá trị lớn hơn bị kẹp).
     */
    virtual common::Status set_brightness(uint8_t percent) = 0;

    /** @return Độ sáng hiện tại (phần trăm). */
    virtual uint8_t brightness() const = 0;
};

} // namespace hal

#endif /* HAL_API_BACKLIGHT_HPP */
