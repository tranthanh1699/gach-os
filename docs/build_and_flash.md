# Build & Flash

> Dùng hằng ngày: xem bản tóm tắt [dev_workflow.md](dev_workflow.md) (build, flash, gach shell, vòng phát triển app).

Hướng dẫn build firmware gach-os và nạp xuống board **JC8012P4A1C** (ESP32-P4 10.1", chi tiết phần cứng ở [mục 7](#7-board-jc8012p4a1c-esp32-p4-101)). Mọi thao tác đi qua `./build.sh` ở gốc repo.

## 1. Chuẩn bị (làm 1 lần)

Chọn **một** trong hai cách:

### Cách A — Docker (khuyến nghị, không cần cài ESP-IDF)

```bash
sudo usermod -aG docker $USER     # cho phép chạy docker không cần sudo
sudo usermod -aG dialout $USER    # quyền truy cập cổng serial /dev/ttyUSB*
# Đăng xuất / đăng nhập lại để group có hiệu lực, kiểm tra:
groups | grep -E 'docker|dialout'
docker info >/dev/null && echo "docker OK"
```

Image `gach-os-idf:latest` (dựa trên `espressif/idf:v6.1.1`) được **tự build ở lần chạy đầu tiên** (~vài phút, tải ~2 GB).

### Cách B — ESP-IDF cài trên máy

Cài ESP-IDF **v6.1.x** theo [hướng dẫn chính thức](https://docs.espressif.com/projects/esp-idf/en/v6.1.1/esp32/get-started/linux-macos-setup.html), sau đó mỗi terminal mới:

```bash
. $HOME/esp/esp-idf/export.sh     # đưa idf.py vào PATH
```

Board ESP32-P4 bắt buộc IDF ≥ 6.1: bản cũ hơn thiếu workaround lỗi phần cứng PPA (DIG-734) → UI treo vĩnh viễn khi xoay màn hình.

`./build.sh` tự nhận ra `idf.py` và build native (nhanh hơn Docker). Ép chế độ bằng `--native` / `--docker`.

### Driver USB

Board dùng chip USB-serial **CH340** (cổng USB-UART) → xuất hiện là `/dev/ttyUSB0`. Kiểm tra sau khi cắm cáp:

```bash
ls /dev/ttyUSB* /dev/ttyACM*
```

> **Ubuntu 22.04+**: gói `brltty` hay "chiếm" CH340 làm cổng biến mất ngay sau khi cắm. Gỡ: `sudo apt remove brltty`.

## 2. Build

```bash
./build.sh                 # = ./build.sh build, board mặc định jc8012p4a1c
```

Luồng: layer check → build ESP-IDF trong `platform/esp32` → output tại `build/jc8012p4a1c/`:

| File | Ý nghĩa |
| :--- | :--- |
| `gach_os.bin` | Firmware ứng dụng |
| `gach_os.elf` | Có symbol, dùng để debug / giải mã backtrace |
| `bootloader/bootloader.bin`, `partition_table/partition-table.bin` | Bootloader và bảng phân vùng |
| `flash_args` | Danh sách offset + file để nạp bằng esptool |

Lần build đầu tải LVGL qua Component Manager (cần Internet), các lần sau dùng cache + ccache.

## 3. Flash & xem log

```bash
./build.sh -p /dev/ttyUSB0 flash            # chỉ nạp
./build.sh -p /dev/ttyUSB0 monitor          # chỉ xem log
./build.sh -p /dev/ttyUSB0 flash-monitor    # nạp rồi mở log (thường dùng nhất)
```

- Thoát monitor: **`Ctrl + ]`**.
- `flash` tự build lại nếu code thay đổi.
- Có thể đặt cổng mặc định để khỏi gõ `-p`: `export PORT=/dev/ttyUSB0`.
- Ở chế độ Docker bắt buộc có `-p` (container cần được cấp đúng thiết bị).

Log khởi động đúng sẽ có:

```
I (xxx) board: JC8012P4A1C ready (1280x800, sd=yes, wifi=yes)
I (xxx) main: OS started on JC8012P4A1C
```

Màn hình hiện Launcher (Wi-Fi, Apps, Stopwatch, Touch, Settings và các app cài thêm).

### Nạp thủ công bằng esptool (không cần build lại)

Dùng khi chỉ có sẵn thư mục build (ví dụ artifact từ CI):

```bash
cd build/jc8012p4a1c
python3 -m esptool --chip esp32p4 -p /dev/ttyUSB0 -b 460800 write_flash @flash_args
```

## 4. Thẻ SD và app cài thêm

- Thẻ microSD định dạng **FAT32**, cắm trước khi bật nguồn. Log boot có `sd=yes`.
- OS tự tạo thư mục `/install` (chép gói `.gapp` vào đây) và `/apps` (app đã cài) trên thẻ.
- Đóng gói và chép app lên thẻ: `./appstore/gapp.sh install /media/$USER/SDCARD` (xem [appstore/README.md](../appstore/README.md)).
- Log boot sẽ có dòng `main: Runtime: ready, N package(s)`. Chi tiết: [docs/sdk.md](sdk.md).

## 5. Lệnh khác

| Lệnh | Tác dụng |
| :--- | :--- |
| `./build.sh menuconfig` | Mở menuconfig (lưu vào `build/<board>/sdkconfig`, không sửa repo) |
| `./build.sh size` | Thống kê dung lượng theo component |
| `./build.sh clean` | Xóa object, giữ cấu hình |
| `./build.sh fullclean` | Xóa toàn bộ `build/<board>/` (cần khi đổi `sdkconfig.defaults`) |
| `./build.sh check` | Chỉ chạy layer check |
| `./build.sh shell` | Mở bash trong container ESP-IDF |
| `./build.sh --list` | Liệt kê board |
| `./build.sh -b <board> ...` | Chọn board khác |

> Sửa `sdkconfig.defaults` (chung hoặc của board) sẽ **không** được áp vào build cũ → chạy `./build.sh fullclean` rồi build lại.

## 6. Xử lý sự cố

| Triệu chứng | Nguyên nhân / Cách xử lý |
| :--- | :--- |
| `ccache: error: Permission denied` | Cache cũ do bản build.sh trước tạo bằng root. Cập nhật repo rồi chạy `./build.sh fullclean && ./build.sh`; dọn volume cũ: `docker volume rm esp32-os-ccache`. |
| `Không kết nối được Docker daemon` | Chưa thuộc group `docker` hoặc chưa đăng nhập lại (mục 1A). |
| `Không thấy cổng /dev/ttyUSB0` | Cáp chỉ sạc (không có dây data), thiếu driver, hoặc `brltty` chiếm cổng. |
| `Permission denied: '/dev/ttyUSB0'` | Chưa thuộc group `dialout` → `sudo usermod -aG dialout $USER`, đăng nhập lại. |
| `Failed to connect to ESP32: No serial data received` | Giữ nút **BOOT**, nhấn-nhả **RST**, thả BOOT rồi flash lại. Có thể hạ baud: `./build.sh shell` rồi dùng esptool với `-b 115200`. |
| Log `SD card unavailable`, app **Apps** báo `No SD card` | Thẻ chưa cắm, không phải FAT32, hoặc lỏng tiếp xúc. Cắm lại thẻ rồi khởi động lại. |
| App cài thêm hiện **App error** | Lỗi trong mã Lua của app: thông báo hiện ngay trên màn hình và trong log serial (`[<id>] script error: ...`). |
| Backtrace khi crash | `monitor` tự giải mã nhờ `gach_os.elf`; luôn mở monitor từ `./build.sh` để có symbol. |

## 7. Board JC8012P4A1C (ESP32-P4 10.1")

```bash
./build.sh -b jc8012p4a1c build
./build.sh -b jc8012p4a1c -p /dev/ttyACM0 flash-monitor   # cổng USB-UART của board
```

| Hạng mục | Cấu hình ([board_config.hpp](../platform/esp32/boards/jc8012p4a1c/board/src/board_config.hpp)) |
| :--- | :--- |
| Màn hình | JD9365 800x1280 MIPI-DSI 2 lane; OS thấy **1280x800** (xoay 90° bằng PPA phần cứng, ghi thẳng framebuffer PSRAM) |
| Cảm ứng | GSL3680, I2C1 SDA7/SCL8, RST22, INT21 – tọa độ xoay theo màn hình |
| Đèn nền | GPIO23, PWM 20 kHz |
| Thẻ SD | SDMMC slot 0, 4 bit, nguồn LDO_VO4 |
| Wi-Fi | ESP32-C6 qua esp_hosted 2.13.1 (SDIO slot 1, buffer ở PSRAM) + esp_wifi_remote 1.6.3 – chạy với firmware C6 xuất xưởng (log cảnh báo `Version mismatch ... Co-proc [0.0.0]` là bình thường) |
| Flash/PSRAM | 16 MB (app 8 MB) / PSRAM 200 MHz |

| Triệu chứng | Cách xử lý |
| :--- | :--- |
| Ảnh lộn ngược 180° | Đổi `SCREEN_ROTATION` giữa `DEG_90` và `DEG_270` (touch tự xoay theo). |
| Ảnh đúng, chạm bị lật trục | Chỉnh `TOUCH.mirror_x` / `mirror_y`. |
| Treo/abort lúc boot ở `esp_hosted`/`H_SDIO` | Firmware C6 không tương thích → nạp lại firmware slave cho C6 (gói hãng: `8-Burn operation/Burn files/JC8012P4A1_C6.bin`). Không nâng esp_hosted lên 3.x: giao thức RPC-V2 không nói chuyện với firmware C6 xuất xưởng. |
| Bootloader báo chip revision không hỗ trợ | IDF 6.x mặc định chỉ chạy P4 ≥ v3.1; board dùng P4 v1.0 → giữ `CONFIG_ESP32P4_SELECTS_REV_LESS_V3=y` trong sdkconfig của board. |

Driver/BSP gốc của hãng nằm ở [`boards/jc8012p4a1c/vendor/`](../platform/esp32/boards/jc8012p4a1c/vendor/README.md) (chỉ build `esp_lcd_jd9365` + `esp_lcd_touch_gsl3680`).

## 8. Cài app & xem log qua cáp (debug bridge)

Không cần rút thẻ SD: `tools/gach install appstore/dist/<id>.gapp`, xem log bằng `tools/gach logcat`,
shell tương tác `tools/gach shell`. Chi tiết: [debug_bridge.md](debug_bridge.md).
