# L0 Platform – ESP32: hiện thực mcal::* và các dịch vụ HAL (system, Wi-Fi, clock, NVS, thẻ SD) bằng ESP-IDF.
# Đây là tầng DUY NHẤT (cùng board/main) được include header ESP-IDF.
# Chip không có Wi-Fi nội: API esp_wifi_* do esp_wifi_remote cung cấp (xem idf_component.yml).
set(ESP32_MCAL_WIFI_REQUIRES esp_wifi)
if(IDF_TARGET STREQUAL "esp32p4")
    list(APPEND ESP32_MCAL_WIFI_REQUIRES espressif__esp_wifi_remote)
endif()

idf_component_register(SRCS "src/esp32_clock.cpp"
                            "src/esp32_gpio.cpp"
                            "src/esp32_http_client.cpp"
                            "src/esp32_nvs_store.cpp"
                            "src/esp32_pwm.cpp"
                            "src/esp32_sd_filesystem.cpp"
                            "src/esp32_spi.cpp"
                            "src/esp32_system.cpp"
                            "src/esp32_uart_debug_port.cpp"
                            "src/esp32_wifi.cpp"
                       INCLUDE_DIRS "include"
                       REQUIRES common mcal hal_api freertos esp_driver_gpio esp_driver_ledc esp_driver_spi esp_driver_sdspi
                                esp_driver_sdmmc esp_driver_uart esp_event nvs_flash sdmmc
                       PRIV_REQUIRES spi_flash ${ESP32_MCAL_WIFI_REQUIRES} esp_netif lwip fatfs esp_hw_support esp_driver_tsens
                                app_update bootloader_support esp_partition esp_http_client mbedtls
                                esp_app_format esp_psram heap espcoredump)

target_compile_options(${COMPONENT_LIB} PRIVATE ${OS_STRICT_FLAGS})
