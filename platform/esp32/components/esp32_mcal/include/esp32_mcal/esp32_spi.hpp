/**
 * @file    esp32_spi.hpp
 * @brief   MCAL SPI cho họ ESP32 (ESP-IDF spi_master).
 */
#ifndef ESP32_MCAL_ESP32_SPI_HPP
#define ESP32_MCAL_ESP32_SPI_HPP

#include <cstddef>
#include <cstdint>

#include "common/non_copyable.hpp"
#include "driver/gpio.h"  // gpio_num_t: IDF >= 5.3 không còn kéo qua spi_master.h
#include "driver/spi_master.h"
#include "mcal/spi.hpp"

namespace esp32_mcal {

/** @brief Cấu hình một bus SPI vật lý. */
struct SpiBusConfig {
    spi_host_device_t host;
    gpio_num_t sclk;
    gpio_num_t mosi;
    gpio_num_t miso;           /**< GPIO_NUM_NC nếu bus chỉ ghi. */
    size_t max_transfer_bytes; /**< Kích thước 1 transaction tối đa (với DMA). */
    bool use_dma;              /**< false: transaction <= 64 byte, không ràng buộc buffer DMA. */
};

/** @brief Một bus SPI. */
class Esp32SpiBus : private common::NonCopyable {
public:
    Esp32SpiBus() : m_cfg{}, m_is_initialized(false) {}

    /** @brief Khởi tạo bus (gọi 1 lần). */
    common::Status init(const SpiBusConfig& cfg);

    spi_host_device_t host() const { return m_cfg.host; }
    size_t max_transfer_bytes() const { return m_cfg.max_transfer_bytes; }
    bool is_initialized() const { return m_is_initialized; }

private:
    SpiBusConfig m_cfg;
    bool m_is_initialized;
};

/** @brief Cấu hình thiết bị trên bus. */
struct SpiDeviceConfig {
    gpio_num_t cs;
    uint32_t clock_hz;
    uint8_t mode;     /**< SPI mode 0..3. */
    bool half_duplex; /**< true cho thiết bị chỉ ghi (LCD) -> không bị giới hạn tần số đọc. */
};

/** @brief Thiết bị SPI, hiện thực mcal::ISpiDevice. Tự chia nhỏ transaction dài. */
class Esp32SpiDevice final : public mcal::ISpiDevice, private common::NonCopyable {
public:
    Esp32SpiDevice() : m_handle(nullptr), m_max_chunk(0U) {}

    /** @brief Gắn thiết bị vào bus đã init. */
    common::Status init(const Esp32SpiBus& bus, const SpiDeviceConfig& cfg);

    common::Status transfer(const uint8_t* p_tx, uint8_t* p_rx, size_t len) override;

private:
    spi_device_handle_t m_handle;
    size_t m_max_chunk;
};

} // namespace esp32_mcal

#endif /* ESP32_MCAL_ESP32_SPI_HPP */
