# Hook board JC8012P4A1C (được platform/esp32/CMakeLists.txt include trước project()).
# Chỉ nạp driver touch GSL3680 từ vendor/ (không có trên registry). Panel JD9365 dùng bản registry hỗ trợ
# ESP-IDF 6 (board/idf_component.yml); phần còn lại của vendor/ (BSP gốc, camera...) chỉ để tham chiếu.
list(APPEND EXTRA_COMPONENT_DIRS
    "${OS_BOARD_DIR}/vendor/esp_lcd_touch_gsl3680")

# Target esp32p4 khác board ESP32 -> khóa phiên bản component riêng, không ghi đè lock của board khác.
set(OS_DEPENDENCIES_LOCK "${OS_BOARD_DIR}/dependencies.lock")
