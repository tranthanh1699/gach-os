/**
 * @file    esp32_sd_filesystem.hpp
 * @brief   hal::IFileSystem trên thẻ microSD (SPI hoặc SDMMC native, FAT) qua VFS của ESP-IDF.
 */
#ifndef ESP32_MCAL_ESP32_SD_FILESYSTEM_HPP
#define ESP32_MCAL_ESP32_SD_FILESYSTEM_HPP

#include <cstddef>
#include <cstdio>

#include "common/non_copyable.hpp"
#include "driver/gpio.h"
#include "driver/spi_common.h"
#include "hal_api/filesystem.hpp"
#include "sdmmc_cmd.h" // sdmmc_card_t là typedef struct ẩn danh -> không forward-declare được.
#include "soc/soc_caps.h"

namespace esp32_mcal {

#if SOC_SDMMC_HOST_SUPPORTED
/** @brief Cấu hình thẻ SD trên controller SDMMC native (chân IOMUX của slot, không cần khai báo). */
struct SdmmcConfig {
    int slot;              /**< SDMMC_HOST_SLOT_0 / SDMMC_HOST_SLOT_1. */
    uint8_t bus_width;     /**< 1 hoặc 4 bit. */
    int max_freq_khz;      /**< VD SDMMC_FREQ_HIGHSPEED. */
    int pwr_ldo_channel;   /**< Kênh LDO on-chip cấp nguồn thẻ (ESP32-P4), < 0 = không dùng. */
};
#endif

/** @brief Thẻ SD mount tại "/sd". Đường dẫn HAL tương đối được ánh xạ thành "/sd/<path>". */
class Esp32SdFileSystem final : public hal::IFileSystem, private common::NonCopyable {
public:
    static constexpr size_t MAX_OPEN_FILES = 4U;

    Esp32SdFileSystem();

    /**
     * @brief Mount thẻ (bus SPI phải được khởi tạo trước bởi Esp32SpiBus).
     * @return ERR_IO nếu không có thẻ / thẻ không định dạng FAT.
     */
    common::Status mount(spi_host_device_t host, gpio_num_t cs);

#if SOC_SDMMC_HOST_SUPPORTED
    /**
     * @brief Mount thẻ trên controller SDMMC native.
     *
     * @note Controller SDMMC có thể dùng chung với slot khác (VD esp_hosted SDIO tới chip Wi-Fi phụ):
     *       khi mount lỗi, controller KHÔNG bị deinit để không làm sập slot còn lại.
     * @return ERR_IO nếu không có thẻ / thẻ không định dạng FAT / không cấp được nguồn LDO.
     */
    common::Status mount_sdmmc(const SdmmcConfig& cfg);
#endif

    bool is_mounted() const override { return m_is_mounted; }
    common::Status open(const char* p_path, hal::OpenMode mode, hal::FileHandle& handle) override;
    common::Status read(hal::FileHandle handle, uint8_t* p_buf, size_t len, size_t& read_len) override;
    common::Status write(hal::FileHandle handle, const uint8_t* p_data, size_t len) override;
    void close(hal::FileHandle handle) override;
    bool exists(const char* p_path, bool& is_dir) override;
    common::Status list_dir(const char* p_path, hal::DirCallback callback, void* p_ctx) override;
    common::Status make_dir(const char* p_path) override;
    common::Status remove(const char* p_path) override;

private:
    bool to_native(const char* p_path, char* p_out, size_t out_len) const;
    FILE* file_of(hal::FileHandle handle) const;

    FILE* m_files[MAX_OPEN_FILES];
    sdmmc_card_t* m_p_card;
    bool m_is_mounted;
};

} // namespace esp32_mcal

#endif /* ESP32_MCAL_ESP32_SD_FILESYSTEM_HPP */
