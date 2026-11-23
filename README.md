# gach-os

**Hệ điều hành giao diện kiểu điện thoại cho vi điều khiển có màn hình.**

## Sứ mệnh

Mang trải nghiệm của một chiếc điện thoại cổ điển — màn hình chính dạng lưới, ứng dụng, thanh trạng thái, nút Back/Home theo tinh thần **Nokia S60** và **Android** — lên các board ESP32 giá rẻ có màn hình, với ba cam kết:

1. **Không phụ thuộc phần cứng** – ứng dụng và framework không biết chip, panel hay touch controller là gì.
2. **Port dễ dàng** – đổi chip hoặc kích thước màn hình chỉ cần viết lại `platform/<chip>` (MCAL + board), không sửa ứng dụng.
3. **Gọn & kỷ luật** – C++17 tĩnh hoàn toàn (không heap, không exception), phân tầng được cưỡng chế tự động bằng layer matrix.

Nền tảng: **ESP-IDF** · **LVGL 9** · **C++17** · build bằng **Docker** hoặc ESP-IDF native.

## Trạng thái

| Hạng mục | Trạng thái |
| :--- | :--- |
| Board | JC8012P4A1C – ESP32-P4 10.1" JD9365 MIPI-DSI 800x1280 chạy ngang 1280x800, GSL3680, Wi-Fi qua ESP32-C6, PSRAM 32 MB |
| Framework | AppManager (vòng đời + back-stack), Shell (status bar + nav bar), Services (Wi-Fi, thời gian), LVGL port |
| Ứng dụng | Launcher nhiều trang (vuốt ngang), Apps, Stopwatch, Touch test, System info, Settings (Wi-Fi, độ sáng, múi giờ) |
| App cài thêm | Gói `.gapp` (Lua 5.4, gach SDK v7) cài từ thẻ SD hoặc qua cáp bằng `tools/gach` (tối đa 128 app), app **Apps** quản lý cài/gỡ, sandbox + giới hạn RAM/thời gian – [docs/sdk.md](docs/sdk.md) |
| Mạng & giờ | Wi-Fi STA, giờ chuẩn từ NTP Google (`time.google.com`), chọn múi giờ trong Settings (mặc định UTC+7), lưu NVS |
| Cưỡng chế kiến trúc | `tools/layer_check.py` + ESP-IDF component REQUIRES |

## Bắt đầu nhanh

```bash
./build.sh                               # layer check + build (tự dùng Docker nếu chưa cài ESP-IDF)
./build.sh -p /dev/ttyUSB0 flash-monitor # nạp + xem log
./build.sh --list                        # các board hỗ trợ
./build.sh --help
```

Dùng hằng ngày (build, flash, shell `gach` kiểu adb): **[docs/dev_workflow.md](docs/dev_workflow.md)** · cài đặt & xử lý sự cố: [docs/build_and_flash.md](docs/build_and_flash.md).

Docker: user cần thuộc group `docker` (`sudo usermod -aG docker $USER`, đăng nhập lại). Image `gach-os-idf` (dựa trên `espressif/idf:v6.1.1`) được build tự động lần đầu.

## Cấu trúc

```
build.sh                    Điểm vào build duy nhất → platform/<chip>/build.sh
src/                        Code portable (không biết chip)
  apps/        L5           Ứng dụng tích hợp
  runtime/     L4.5         App cài thêm: gói .gapp, máy ảo Lua, gach SDK
  framework/   L4           OS core: AppManager, Shell, Services, LVGL port
  drivers/     L3           Driver IC portable (PWM backlight)
  hal_api/     L2           Contract thiết bị + hal::board_init()
  mcal/ osal/  L1           Contract ngoại vi & RTOS
  common/      L0           Kiểu dùng chung
platform/esp32/             ESP-IDF project cho họ ESP32
  components/esp32_mcal/    MCAL bằng ESP-IDF (SPI, GPIO, LEDC, system)
  boards/jc8012p4a1c/       ESP32-P4 10.1": panel DSI + xoay PPA, touch GSL3680, vendor/ (BSP gốc của hãng)
  main/                     app_main()
tools/                      layer_matrix.json, layer_check.py (+ test), gapp_pack.py
appstore/                   Kho app cài thêm: apps/<tên> (calculator, counter, world_clock), gapp.sh
third_party/lua/            Lua 5.4.7 (MIT)
docker/                     Dockerfile môi trường build
docs/architecture.md        Kiến trúc, layer matrix, hướng dẫn port
```

Chi tiết: [docs/architecture.md](docs/architecture.md) · Quy tắc cho AI agent & coding: [CLAUDE.md](CLAUDE.md).

## Lộ trình

- [ ] Platform `host` (SDL2) để chạy/kiểm thử UI trên PC
- [ ] Flush DMA bất đồng bộ + double buffer
- [x] Wi-Fi, giờ NTP, lưu cài đặt (NVS) qua framework services
- [x] Chọn múi giờ trong Settings
- [x] App cài thêm từ thẻ SD (runtime Lua + gach SDK v2)
- [ ] Kho ứng dụng: tải `.gapp` qua Wi-Fi về `/install` + danh mục từ server
- [ ] Board thứ hai (ESP32-S3 + ST7789/GT911) để chứng minh tính port
