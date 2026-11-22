# Board component (tên component luôn là "board"; build chỉ nạp board được chọn qua BOARD).
idf_component_register(SRCS "src/board.cpp"
                       PRIV_INCLUDE_DIRS "src"
                       REQUIRES hal_api
                       PRIV_REQUIRES common mcal drivers esp32_mcal log)

target_compile_options(${COMPONENT_LIB} PRIVATE ${OS_STRICT_FLAGS})
