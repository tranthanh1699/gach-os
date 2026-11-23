# Board component JC8012P4A1C (tên component luôn là "board"; build chỉ nạp board được chọn qua BOARD).
# Panel MIPI-DSI + PPA + touch GSL3680 gắn chặt SDK ESP32-P4 -> hiện thực ngay trong board.
idf_component_register(SRCS "src/board.cpp"
                            "src/gsl3680_touch.cpp"
                            "src/jd9365_display.cpp"
                            "src/jd9365_init_cmds.c"
                       PRIV_INCLUDE_DIRS "src"
                       REQUIRES hal_api
                       PRIV_REQUIRES common mcal drivers esp32_mcal log heap esp_mm esp_hw_support
                                     esp_lcd esp_driver_ppa esp_driver_i2c esp_driver_sdmmc
                                     espressif__esp_lcd_jd9365 esp_lcd_touch_gsl3680)

target_compile_options(${COMPONENT_LIB} PRIVATE ${OS_STRICT_FLAGS})
