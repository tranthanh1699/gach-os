/**
 * @file    esp32_spi.cpp
 * @brief   Hiện thực MCAL SPI trên ESP-IDF.
 */
#include "esp32_mcal/esp32_spi.hpp"

#include <algorithm>

namespace esp32_mcal {

namespace {

constexpr size_t POLLING_THRESHOLD_BYTES = 32U; // Transaction ngắn: polling nhanh hơn ngắt.
constexpr size_t NON_DMA_MAX_BYTES = 64U;       // Giới hạn FIFO khi không dùng DMA.
constexpr size_t BITS_PER_BYTE = 8U;
constexpr int QUEUE_SIZE = 1;

} // namespace

common::Status Esp32SpiBus::init(const SpiBusConfig& cfg)
{
    if (m_is_initialized) {
        return common::Status::ERR_STATE;
    }
    m_cfg = cfg;
    if (!m_cfg.use_dma) {
        m_cfg.max_transfer_bytes = std::min(m_cfg.max_transfer_bytes, NON_DMA_MAX_BYTES);
    }

    spi_bus_config_t bus_cfg{};
    bus_cfg.sclk_io_num = cfg.sclk;
    bus_cfg.mosi_io_num = cfg.mosi;
    bus_cfg.miso_io_num = cfg.miso;
    bus_cfg.quadwp_io_num = GPIO_NUM_NC;
    bus_cfg.quadhd_io_num = GPIO_NUM_NC;
    bus_cfg.max_transfer_sz = static_cast<int>(m_cfg.max_transfer_bytes);

    const spi_dma_chan_t dma = cfg.use_dma ? SPI_DMA_CH_AUTO : SPI_DMA_DISABLED;
    if (spi_bus_initialize(cfg.host, &bus_cfg, dma) != ESP_OK) {
        return common::Status::ERR_IO;
    }
    m_is_initialized = true;
    return common::Status::OK;
}

common::Status Esp32SpiDevice::init(const Esp32SpiBus& bus, const SpiDeviceConfig& cfg)
{
    if (!bus.is_initialized() || (m_handle != nullptr)) {
        return common::Status::ERR_STATE;
    }
    spi_device_interface_config_t dev_cfg{};
    dev_cfg.mode = cfg.mode;
    dev_cfg.clock_speed_hz = static_cast<int>(cfg.clock_hz);
    dev_cfg.spics_io_num = cfg.cs;
    dev_cfg.queue_size = QUEUE_SIZE;
    dev_cfg.flags = cfg.half_duplex ? SPI_DEVICE_HALFDUPLEX : 0U;

    if (spi_bus_add_device(bus.host(), &dev_cfg, &m_handle) != ESP_OK) {
        m_handle = nullptr;
        return common::Status::ERR_IO;
    }
    m_max_chunk = bus.max_transfer_bytes();
    return common::Status::OK;
}

common::Status Esp32SpiDevice::transfer(const uint8_t* p_tx, uint8_t* p_rx, size_t len)
{
    if (m_handle == nullptr) {
        return common::Status::ERR_STATE;
    }
    if ((p_tx == nullptr) || (len == 0U)) {
        return common::Status::ERR_PARAM;
    }

    size_t offset = 0U;
    while (offset < len) {
        const size_t chunk = std::min(len - offset, m_max_chunk);
        spi_transaction_t trans{};
        trans.length = chunk * BITS_PER_BYTE;
        trans.tx_buffer = p_tx + offset;
        trans.rx_buffer = (p_rx != nullptr) ? (p_rx + offset) : nullptr;

        const esp_err_t err = (chunk <= POLLING_THRESHOLD_BYTES) ? spi_device_polling_transmit(m_handle, &trans)
                                                                 : spi_device_transmit(m_handle, &trans);
        if (err != ESP_OK) {
            return common::Status::ERR_IO;
        }
        offset += chunk;
    }
    return common::Status::OK;
}

} // namespace esp32_mcal
