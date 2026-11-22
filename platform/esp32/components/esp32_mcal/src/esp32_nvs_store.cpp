/**
 * @file    esp32_nvs_store.cpp
 * @brief   Hiện thực kho key-value trên NVS.
 */
#include "esp32_mcal/esp32_nvs_store.hpp"

#include "nvs_flash.h"

namespace esp32_mcal {

namespace {

common::Status to_status(esp_err_t err)
{
    switch (err) {
    case ESP_OK:
        return common::Status::OK;
    case ESP_ERR_NVS_NOT_FOUND:
        return common::Status::ERR_NOT_FOUND;
    case ESP_ERR_NVS_INVALID_LENGTH:
        return common::Status::ERR_NO_MEM;
    case ESP_ERR_NVS_INVALID_NAME:
    case ESP_ERR_NVS_KEY_TOO_LONG:
        return common::Status::ERR_PARAM;
    default:
        return common::Status::ERR_IO;
    }
}

} // namespace

common::Status Esp32NvsStore::init(const char* p_namespace)
{
    if (m_is_open) {
        return common::Status::ERR_STATE;
    }
    esp_err_t err = nvs_flash_init();
    if ((err == ESP_ERR_NVS_NO_FREE_PAGES) || (err == ESP_ERR_NVS_NEW_VERSION_FOUND)) {
        // Phân vùng đầy/khác phiên bản: xóa để hệ thống vẫn khởi động được (mất cài đặt cũ).
        (void)nvs_flash_erase();
        err = nvs_flash_init();
    }
    if (err == ESP_OK) {
        err = nvs_open(p_namespace, NVS_READWRITE, &m_handle);
    }
    m_is_open = (err == ESP_OK);
    return to_status(err);
}

common::Status Esp32NvsStore::get_string(const char* p_key, char* p_buf, size_t buf_len) const
{
    if (!m_is_open) {
        return common::Status::ERR_STATE;
    }
    size_t length = buf_len;
    return to_status(nvs_get_str(m_handle, p_key, p_buf, &length));
}

common::Status Esp32NvsStore::set_string(const char* p_key, const char* p_value)
{
    if (!m_is_open) {
        return common::Status::ERR_STATE;
    }
    esp_err_t err = nvs_set_str(m_handle, p_key, p_value);
    if (err == ESP_OK) {
        err = nvs_commit(m_handle);
    }
    return to_status(err);
}

common::Status Esp32NvsStore::get_i32(const char* p_key, int32_t& value) const
{
    if (!m_is_open) {
        return common::Status::ERR_STATE;
    }
    return to_status(nvs_get_i32(m_handle, p_key, &value));
}

common::Status Esp32NvsStore::set_i32(const char* p_key, int32_t value)
{
    if (!m_is_open) {
        return common::Status::ERR_STATE;
    }
    esp_err_t err = nvs_set_i32(m_handle, p_key, value);
    if (err == ESP_OK) {
        err = nvs_commit(m_handle);
    }
    return to_status(err);
}

common::Status Esp32NvsStore::erase(const char* p_key)
{
    if (!m_is_open) {
        return common::Status::ERR_STATE;
    }
    esp_err_t err = nvs_erase_key(m_handle, p_key);
    if (err == ESP_ERR_NVS_NOT_FOUND) {
        return common::Status::OK;
    }
    if (err == ESP_OK) {
        err = nvs_commit(m_handle);
    }
    return to_status(err);
}

} // namespace esp32_mcal
