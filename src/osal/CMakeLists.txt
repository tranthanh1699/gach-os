# L1 OSAL. Interface ở include/osal, port chọn qua OSAL_PORT (mặc định freertos).
if(NOT DEFINED OSAL_PORT)
    set(OSAL_PORT "freertos")
endif()

idf_component_register(SRCS "port/${OSAL_PORT}/osal_${OSAL_PORT}.cpp"
                       INCLUDE_DIRS "include"
                       REQUIRES common freertos)

target_compile_options(${COMPONENT_LIB} PRIVATE ${OS_STRICT_FLAGS})
