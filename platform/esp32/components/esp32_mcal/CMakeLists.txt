# L0 Platform – ESP32: hiện thực mcal::* và các dịch vụ HAL (system, Wi-Fi, clock, NVS, thẻ SD) bằng ESP-IDF.
# Đây là tầng DUY NHẤT (cùng board/main) được include header ESP-IDF.
idf_component_register(SRCS "src/esp32_clock.cpp"
                            "src/esp32_gpio.cpp"
                            "src/esp32_nvs_store.cpp"
                            "src/esp32_pwm.cpp"
                            "src/esp32_sd_filesystem.cpp"
                            "src/esp32_spi.cpp"
                            "src/esp32_system.cpp"
                            "src/esp32_wifi.cpp"
                       INCLUDE_DIRS "include"
                       REQUIRES common mcal hal_api driver esp_event nvs_flash sdmmc
                       PRIV_REQUIRES spi_flash esp_wifi esp_netif lwip fatfs)

target_compile_options(${COMPONENT_LIB} PRIVATE ${OS_STRICT_FLAGS})
