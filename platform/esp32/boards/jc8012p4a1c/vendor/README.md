# Vendor BSP – JC8012P4A1C

Bản sao nguyên trạng `1-Demo/idf-examples/common_components` từ gói demo của hãng (ESP-IDF 5.4).
**Không sửa trực tiếp** – khi cập nhật, chép đè cả thư mục từ gói mới.

| Thư mục | Vai trò | Được build? |
|---|---|---|
| `esp_lcd_jd9365/` | Driver panel MIPI-DSI JD9365 800x1280 (chỉ chạy IDF 5.x) | Không – dùng bản registry 2.0.2; **chuỗi init** chép sang `../board/src/jd9365_init_cmds.c` |
| `esp_lcd_touch_gsl3680/` | Driver cảm ứng GSL3680 (I2C 0x40) | **Có** (qua `../board.cmake`) |
| `esp32_p4_function_ev_board/` | BSP gốc của hãng (chân, init LCD/touch/SD/audio, kèm esp_lvgl_port) | Không – chỉ tham chiếu |
| `bsp_extra/` | Tiện ích audio/file của hãng | Không |
| `espressif__esp_lcd_jd9165/` | Driver panel 1024x600 (biến thể board khác) | Không |
| `esp_video/`, `esp_cam_sensor/` | Camera MIPI-CSI | Không |

BSP gốc tự khởi tạo LVGL + task riêng (`esp_lvgl_port`), xung đột với framework gach-os (framework sở hữu LVGL),
nên sơ đồ chân và thông số được port sang `../board/src/board_config.hpp`, còn panel/touch được bọc lại
thành `hal::IDisplay` / `hal::ITouch` trong `../board/src/`.
