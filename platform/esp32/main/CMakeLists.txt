idf_component_register(SRCS "main.cpp"
                       LDFRAGMENTS "runtime_psram.lf"
                       PRIV_REQUIRES common hal_api framework runtime apps board log)

target_compile_options(${COMPONENT_LIB} PRIVATE ${OS_STRICT_FLAGS})
