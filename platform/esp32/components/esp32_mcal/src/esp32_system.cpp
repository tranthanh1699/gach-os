/**
 * @file    esp32_system.cpp
 * @brief   Hiện thực hal::ISystem trên ESP-IDF.
 */
#include "esp32_mcal/esp32_system.hpp"

#include "esp_chip_info.h"
#include "esp_flash.h"
#include "esp_system.h"
#include "sdkconfig.h"

namespace esp32_mcal {

namespace {
constexpr uint32_t BYTES_PER_KIB = 1024U;
}

void Esp32System::get_info(hal::SystemInfo& info) const
{
    esp_chip_info_t chip{};
    esp_chip_info(&chip);

    uint32_t flash_bytes = 0U;
    if (esp_flash_get_size(nullptr, &flash_bytes) != ESP_OK) {
        flash_bytes = 0U;
    }

    info.p_chip_name = CONFIG_IDF_TARGET;
    info.cpu_freq_mhz = static_cast<uint32_t>(CONFIG_ESP_DEFAULT_CPU_FREQ_MHZ);
    info.cpu_cores = chip.cores;
    info.flash_size_kb = flash_bytes / BYTES_PER_KIB;
    info.heap_free_bytes = esp_get_free_heap_size();
    info.heap_min_bytes = esp_get_minimum_free_heap_size();
}

void Esp32System::restart()
{
    esp_restart();
}

} // namespace esp32_mcal
