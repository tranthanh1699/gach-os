/**
 * @file    esp32_gpio.hpp
 * @brief   MCAL GPIO cho họ ESP32.
 */
#ifndef ESP32_MCAL_ESP32_GPIO_HPP
#define ESP32_MCAL_ESP32_GPIO_HPP

#include "common/non_copyable.hpp"
#include "common/status.hpp"
#include "driver/gpio.h"
#include "mcal/gpio.hpp"

namespace esp32_mcal {

/** @brief GPIO output. */
class Esp32GpioOutput final : public mcal::IGpioOutput, private common::NonCopyable {
public:
    Esp32GpioOutput() : m_pin(GPIO_NUM_NC) {}

    /** @brief Cấu hình chân output và đặt mức ban đầu. */
    common::Status init(gpio_num_t pin, mcal::PinLevel initial);

    void write(mcal::PinLevel level) override;

private:
    gpio_num_t m_pin;
};

/** @brief GPIO input. */
class Esp32GpioInput final : public mcal::IGpioInput, private common::NonCopyable {
public:
    Esp32GpioInput() : m_pin(GPIO_NUM_NC) {}

    /** @brief Cấu hình chân input. @param[in] pull_up Bật điện trở kéo lên nội (nếu chân hỗ trợ). */
    common::Status init(gpio_num_t pin, bool pull_up);

    mcal::PinLevel read() const override;

private:
    gpio_num_t m_pin;
};

} // namespace esp32_mcal

#endif /* ESP32_MCAL_ESP32_GPIO_HPP */
