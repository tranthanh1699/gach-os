/**
 * @file    esp32_system.hpp
 * @brief   hal::ISystem cho họ ESP32 (thông tin chip, heap, restart).
 */
#ifndef ESP32_MCAL_ESP32_SYSTEM_HPP
#define ESP32_MCAL_ESP32_SYSTEM_HPP

#include "hal_api/system.hpp"

namespace esp32_mcal {

/** @brief Dịch vụ hệ thống ESP-IDF. */
class Esp32System final : public hal::ISystem {
public:
    void get_info(hal::SystemInfo& info) const override;
    void restart() override;
};

} // namespace esp32_mcal

#endif /* ESP32_MCAL_ESP32_SYSTEM_HPP */
