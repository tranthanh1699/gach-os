/**
 * @file    esp32_sd_filesystem.hpp
 * @brief   hal::IFileSystem trên thẻ microSD (SPI, FAT) qua VFS của ESP-IDF.
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

namespace esp32_mcal {

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
