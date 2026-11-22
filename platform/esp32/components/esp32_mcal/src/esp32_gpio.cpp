/**
 * @file    esp32_gpio.cpp
 * @brief   Hiện thực MCAL GPIO trên ESP-IDF.
 */
#include "esp32_mcal/esp32_gpio.hpp"

namespace esp32_mcal {

common::Status Esp32GpioOutput::init(gpio_num_t pin, mcal::PinLevel initial)
{
    gpio_config_t cfg{};
    cfg.pin_bit_mask = 1ULL << static_cast<uint32_t>(pin);
    cfg.mode = GPIO_MODE_OUTPUT;
    cfg.pull_up_en = GPIO_PULLUP_DISABLE;
    cfg.pull_down_en = GPIO_PULLDOWN_DISABLE;
    cfg.intr_type = GPIO_INTR_DISABLE;
    if (gpio_config(&cfg) != ESP_OK) {
        return common::Status::ERR_IO;
    }
    m_pin = pin;
    write(initial);
    return common::Status::OK;
}

void Esp32GpioOutput::write(mcal::PinLevel level)
{
    if (m_pin != GPIO_NUM_NC) {
        (void)gpio_set_level(m_pin, (level == mcal::PinLevel::HIGH) ? 1U : 0U);
    }
}

common::Status Esp32GpioInput::init(gpio_num_t pin, bool pull_up)
{
    gpio_config_t cfg{};
    cfg.pin_bit_mask = 1ULL << static_cast<uint32_t>(pin);
    cfg.mode = GPIO_MODE_INPUT;
    cfg.pull_up_en = pull_up ? GPIO_PULLUP_ENABLE : GPIO_PULLUP_DISABLE;
    cfg.pull_down_en = GPIO_PULLDOWN_DISABLE;
    cfg.intr_type = GPIO_INTR_DISABLE;
    if (gpio_config(&cfg) != ESP_OK) {
        return common::Status::ERR_IO;
    }
    m_pin = pin;
    return common::Status::OK;
}

mcal::PinLevel Esp32GpioInput::read() const
{
    return (gpio_get_level(m_pin) != 0) ? mcal::PinLevel::HIGH : mcal::PinLevel::LOW;
}

} // namespace esp32_mcal
