# L3 Device drivers portable: chỉ dùng MCAL contract + OSAL, không include SDK của chip.
idf_component_register(SRCS "src/pwm_backlight.cpp"
                       INCLUDE_DIRS "include"
                       REQUIRES common mcal osal hal_api)

target_compile_options(${COMPONENT_LIB} PRIVATE ${OS_STRICT_FLAGS})
