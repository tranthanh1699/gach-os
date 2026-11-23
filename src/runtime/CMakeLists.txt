# L4.5 Runtime: app cài thêm (gói .gapp) chạy trên máy ảo Lua, cung cấp gach SDK.
# Phụ thuộc: framework + HAL contract (filesystem) + OSAL + LVGL + Lua. Không SDK chip.
idf_component_register(SRCS "src/arena_allocator.cpp"
                            "src/debug_bridge.cpp"
                            "src/fs_utils.cpp"
                            "src/log_history.cpp"
                            "src/manifest.cpp"
                            "src/package_archive.cpp"
                            "src/runtime.cpp"
                            "src/script_app.cpp"
                            "src/script_host.cpp"
                            "src/script_ui.cpp"
                       INCLUDE_DIRS "include"
                       PRIV_INCLUDE_DIRS "src"
                       REQUIRES common
                       PRIV_REQUIRES framework hal_api osal lua lvgl__lvgl)

target_compile_options(${COMPONENT_LIB} PRIVATE ${OS_STRICT_FLAGS})
