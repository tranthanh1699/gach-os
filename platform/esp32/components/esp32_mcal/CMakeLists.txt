# L0 Platform – MCAL ESP32: hiện thực mcal::* và hal::ISystem bằng ESP-IDF.
# Đây là tầng DUY NHẤT (cùng board/main) được include header ESP-IDF.
idf_component_register(SRCS "src/esp32_gpio.cpp"
                            "src/esp32_pwm.cpp"
                            "src/esp32_spi.cpp"
                            "src/esp32_system.cpp"
                       INCLUDE_DIRS "include"
                       REQUIRES common mcal hal_api driver
                       PRIV_REQUIRES spi_flash)

target_compile_options(${COMPONENT_LIB} PRIVATE ${OS_STRICT_FLAGS})
