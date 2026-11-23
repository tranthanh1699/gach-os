/**
 * @file    esp32_uart_debug_port.cpp
 * @brief   Hiện thực cổng debug trên UART console.
 */
#include "esp32_mcal/esp32_uart_debug_port.hpp"

#include "driver/uart.h"
#include "driver/uart_vfs.h"
#include "freertos/FreeRTOS.h"
#include "sdkconfig.h"

namespace esp32_mcal {

namespace {

constexpr uart_port_t PORT = static_cast<uart_port_t>(CONFIG_ESP_CONSOLE_UART_NUM);
constexpr int RX_BUFFER_BYTES = 4096; /**< Đủ cho vài khung 256 B khi task bridge chưa kịp đọc. */
constexpr int TX_BUFFER_BYTES = 0;    /**< Ghi trực tiếp FIFO (blocking) -> giữ khóa TX trọn một lần ghi. */

} // namespace

common::Status Esp32UartDebugPort::init()
{
    if (m_is_initialized) {
        return common::Status::ERR_STATE;
    }
    if (uart_driver_install(PORT, RX_BUFFER_BYTES, TX_BUFFER_BYTES, 0, nullptr, 0) != ESP_OK) {
        return common::Status::ERR_IO;
    }
    uart_vfs_dev_use_driver(PORT); // printf/ESP_LOG cũng đi qua driver (cùng khóa TX với khung bridge).
    m_is_initialized = true;
    return common::Status::OK;
}

size_t Esp32UartDebugPort::read(uint8_t* p_buf, size_t len, uint32_t timeout_ms)
{
    if (!m_is_initialized || (p_buf == nullptr) || (len == 0U)) {
        return 0U;
    }
    // uart_read_bytes() chờ ĐỦ len byte (hoặc hết giờ) -> khung nhỏ bị giữ cả timeout. Thay vào đó: chờ byte
    // đầu tiên, rồi lấy ngay phần đã có trong buffer.
    size_t available = 0U;
    (void)uart_get_buffered_data_len(PORT, &available);
    size_t total = 0U;
    if (available == 0U) {
        if (uart_read_bytes(PORT, p_buf, 1U, pdMS_TO_TICKS(timeout_ms)) <= 0) {
            return 0U;
        }
        total = 1U;
        (void)uart_get_buffered_data_len(PORT, &available);
    }
    const size_t want = ((len - total) < available) ? (len - total) : available;
    if (want > 0U) {
        const int got = uart_read_bytes(PORT, &p_buf[total], static_cast<uint32_t>(want), 0);
        total += (got > 0) ? static_cast<size_t>(got) : 0U;
    }
    return total;
}

common::Status Esp32UartDebugPort::write(const uint8_t* p_data, size_t len)
{
    if (!m_is_initialized) {
        return common::Status::ERR_STATE;
    }
    const int written = uart_write_bytes(PORT, p_data, len);
    return (written == static_cast<int>(len)) ? common::Status::OK : common::Status::ERR_IO;
}

} // namespace esp32_mcal
