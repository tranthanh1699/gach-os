idf_component_register(SRCS "main.cpp"
                       PRIV_REQUIRES common hal_api framework apps board log)

target_compile_options(${COMPONENT_LIB} PRIVATE ${OS_STRICT_FLAGS})
