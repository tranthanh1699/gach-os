# L5 Applications: CHỈ phụ thuộc framework + runtime (API public) + LVGL. Không HAL, không driver, không SDK chip.
idf_component_register(SRCS "src/gach_store_app.cpp"
                            "src/builtin_apps.cpp"
                            "src/files_app.cpp"
                            "src/launcher_app.cpp"
                            "src/settings_app.cpp"
                            "src/stopwatch_app.cpp"
                            "src/touch_test_app.cpp"
                            "src/wifi_app.cpp"
                       INCLUDE_DIRS "include"
                       PRIV_INCLUDE_DIRS "src"
                       REQUIRES common framework
                       PRIV_REQUIRES lvgl__lvgl runtime)

target_compile_options(${COMPONENT_LIB} PRIVATE ${OS_STRICT_FLAGS})
