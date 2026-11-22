# L3 Device drivers portable: chỉ dùng MCAL contract + OSAL, không include SDK của chip.
idf_component_register(SRCS "src/ili9341.cpp"
                            "src/xpt2046.cpp"
                            "src/pwm_backlight.cpp"
                            "src/soft_spi.cpp"
                       INCLUDE_DIRS "include"
                       REQUIRES common mcal osal hal_api)

target_compile_options(${COMPONENT_LIB} PRIVATE ${OS_STRICT_FLAGS})
