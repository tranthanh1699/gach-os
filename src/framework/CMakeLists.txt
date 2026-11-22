# L4 Framework (OS core): AppManager, Shell, Services, LVGL port.
# Phụ thuộc: HAL contract + OSAL + LVGL. KHÔNG phụ thuộc drivers/mcal/SDK chip.
idf_component_register(SRCS "src/app_manager.cpp"
                            "src/lvgl_port.cpp"
                            "src/services.cpp"
                            "src/shell.cpp"
                            "src/system.cpp"
                       INCLUDE_DIRS "include"
                       PRIV_INCLUDE_DIRS "src"
                       REQUIRES common lvgl__lvgl
                       PRIV_REQUIRES hal_api osal)

target_compile_options(${COMPONENT_LIB} PRIVATE ${OS_STRICT_FLAGS})
