# esp32-os

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
| Board | ESP32-2432S028R ("Cheap Yellow Display") – ILI9341 320x240, XPT2046 |
| Framework | AppManager (vòng đời + back-stack), Shell (status bar + nav bar), Services, LVGL port |
| Ứng dụng | Launcher, Stopwatch, Touch test, System info, Settings (độ sáng, restart) |
| Cưỡng chế kiến trúc | `tools/layer_check.py` + ESP-IDF component REQUIRES |

## Bắt đầu nhanh

```bash
./build.sh                               # layer check + build (tự dùng Docker nếu chưa cài ESP-IDF)
./build.sh -p /dev/ttyUSB0 flash-monitor # nạp + xem log
./build.sh --list                        # các board hỗ trợ
./build.sh --help
```

Hướng dẫn đầy đủ (cài đặt, flash, xử lý sự cố): **[docs/build_and_flash.md](docs/build_and_flash.md)**.

Docker: user cần thuộc group `docker` (`sudo usermod -aG docker $USER`, đăng nhập lại). Image `esp32-os-idf` (dựa trên `espressif/idf:v5.4.1`) được build tự động lần đầu.

## Cấu trúc

```
build.sh                    Điểm vào build duy nhất → platform/<chip>/build.sh
src/                        Code portable (không biết chip)
  apps/        L5           Ứng dụng
  framework/   L4           OS core: AppManager, Shell, Services, LVGL port
  drivers/     L3           Driver IC: ILI9341, XPT2046, PWM backlight
  hal_api/     L2           Contract thiết bị + hal::board_init()
  mcal/ osal/  L1           Contract ngoại vi & RTOS
  common/      L0           Kiểu dùng chung
platform/esp32/             ESP-IDF project cho họ ESP32
  components/esp32_mcal/    MCAL bằng ESP-IDF (SPI, GPIO, LEDC, system)
  boards/esp32_2432s028/    Pin map, hiệu chuẩn, composition root
  main/                     app_main()
tools/                      layer_matrix.json, layer_check.py (+ test)
docker/                     Dockerfile môi trường build
docs/architecture.md        Kiến trúc, layer matrix, hướng dẫn port
```

Chi tiết: [docs/architecture.md](docs/architecture.md) · Quy tắc cho AI agent & coding: [CLAUDE.md](CLAUDE.md).

## Lộ trình

- [ ] Platform `host` (SDL2) để chạy/kiểm thử UI trên PC
- [ ] Flush DMA bất đồng bộ + double buffer
- [ ] Dịch vụ thời gian (RTC/SNTP), Wi-Fi, lưu cài đặt (NVS) qua framework services
- [ ] Board thứ hai (ESP32-S3 + ST7789/GT911) để chứng minh tính port
