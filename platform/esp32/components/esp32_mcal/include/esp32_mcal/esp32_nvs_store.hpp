/**
 * @file    esp32_nvs_store.hpp
 * @brief   hal::IKeyValueStore trên NVS của ESP-IDF.
 */
#ifndef ESP32_MCAL_ESP32_NVS_STORE_HPP
#define ESP32_MCAL_ESP32_NVS_STORE_HPP

#include "common/non_copyable.hpp"
#include "hal_api/storage.hpp"
#include "nvs.h"

namespace esp32_mcal {

/** @brief Kho key-value NVS (một namespace). */
class Esp32NvsStore final : public hal::IKeyValueStore, private common::NonCopyable {
public:
    Esp32NvsStore() : m_handle(0U), m_is_open(false) {}

    /**
     * @brief Khởi tạo phân vùng NVS (tự xóa nếu hỏng/đổi phiên bản) và mở namespace.
     * @note  Bắt buộc gọi trước khi khởi tạo Wi-Fi (Wi-Fi driver cần NVS).
     */
    common::Status init(const char* p_namespace);

    common::Status get_string(const char* p_key, char* p_buf, size_t buf_len) const override;
    common::Status set_string(const char* p_key, const char* p_value) override;
    common::Status get_i32(const char* p_key, int32_t& value) const override;
    common::Status set_i32(const char* p_key, int32_t value) override;
    common::Status erase(const char* p_key) override;

private:
    nvs_handle_t m_handle;
    bool m_is_open;
};

} // namespace esp32_mcal

#endif /* ESP32_MCAL_ESP32_NVS_STORE_HPP */
