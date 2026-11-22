# ==============================================================================
# ESP-IDF project cho họ chip ESP32 (esp32, esp32s3, esp32c3...).
#   - Code portable : <repo>/src/*            (apps, framework, hal_api, drivers, mcal, osal, common)
#   - MCAL ESP32    : components/esp32_mcal
#   - Board         : boards/<BOARD>/board    (chọn qua -DBOARD=<name>)
# Không gọi trực tiếp: dùng <repo>/build.sh hoặc platform/esp32/build.sh.
# ==============================================================================
cmake_minimum_required(VERSION 3.16)

if(NOT DEFINED BOARD)
    set(BOARD "esp32_2432s028")
endif()

set(OS_ROOT_DIR "${CMAKE_CURRENT_LIST_DIR}/../..")
set(OS_BOARD_DIR "${CMAKE_CURRENT_LIST_DIR}/boards/${BOARD}")
if(NOT EXISTS "${OS_BOARD_DIR}/board/CMakeLists.txt")
    message(FATAL_ERROR "Board '${BOARD}' không tồn tại trong ${CMAKE_CURRENT_LIST_DIR}/boards")
endif()

set(EXTRA_COMPONENT_DIRS
    "${OS_ROOT_DIR}/src"
    "${CMAKE_CURRENT_LIST_DIR}/components"
    "${OS_BOARD_DIR}/board")

# Cờ cảnh báo nghiêm ngặt cho code của dự án (không áp lên component ESP-IDF/LVGL).
set(OS_STRICT_FLAGS -Wall -Wextra -Werror -Wshadow -Wnon-virtual-dtor -Wno-missing-field-initializers)

include($ENV{IDF_PATH}/tools/cmake/project.cmake)
project(gach_os)
