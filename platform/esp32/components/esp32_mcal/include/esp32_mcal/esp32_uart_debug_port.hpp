/**
 * @file    esp32_uart_debug_port.hpp
 * @brief   hal::IDebugPort trên UART console (cáp USB-serial dùng để nạp firmware).
 *
 * Cài driver UART cho cổng console và chuyển VFS (printf/ESP_LOG) sang đi qua driver: mọi lần ghi
 * (khung của debug bridge lẫn printf thô) đều qua cùng một khóa TX -> không xen byte giữa các khung.
 */
#ifndef ESP32_MCAL_ESP32_UART_DEBUG_PORT_HPP
#define ESP32_MCAL_ESP32_UART_DEBUG_PORT_HPP

#include <cstddef>
#include <cstdint>

#include "common/non_copyable.hpp"
#include "hal_api/debug_port.hpp"

namespace esp32_mcal {

/** @brief Cổng debug trên UART console. */
class Esp32UartDebugPort final : public hal::IDebugPort, private common::NonCopyable {
public:
    Esp32UartDebugPort() : m_is_initialized(false) {}

    common::Status init() override;
    size_t read(uint8_t* p_buf, size_t len, uint32_t timeout_ms) override;
    common::Status write(const uint8_t* p_data, size_t len) override;

private:
    bool m_is_initialized;
};

} // namespace esp32_mcal

#endif /* ESP32_MCAL_ESP32_UART_DEBUG_PORT_HPP */
