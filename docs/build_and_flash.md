# Build & Flash

Hướng dẫn build firmware gach-os và nạp xuống board **ESP32-2432S028R (CYD)**. Mọi thao tác đi qua `./build.sh` ở gốc repo.

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

Image `gach-os-idf:latest` (dựa trên `espressif/idf:v5.4.1`) được **tự build ở lần chạy đầu tiên** (~vài phút, tải ~2 GB).

### Cách B — ESP-IDF cài trên máy

Cài ESP-IDF **v5.4.x** theo [hướng dẫn chính thức](https://docs.espressif.com/projects/esp-idf/en/v5.4.1/esp32/get-started/linux-macos-setup.html), sau đó mỗi terminal mới:

```bash
. $HOME/esp/esp-idf/export.sh     # đưa idf.py vào PATH
```

`./build.sh` tự nhận ra `idf.py` và build native (nhanh hơn Docker). Ép chế độ bằng `--native` / `--docker`.

### Driver USB

CYD dùng chip USB-serial **CH340** → xuất hiện là `/dev/ttyUSB0`. Kiểm tra sau khi cắm cáp:

```bash
ls /dev/ttyUSB* /dev/ttyACM*
```

> **Ubuntu 22.04+**: gói `brltty` hay "chiếm" CH340 làm cổng biến mất ngay sau khi cắm. Gỡ: `sudo apt remove brltty`.

## 2. Build

```bash
./build.sh                 # = ./build.sh build, board mặc định esp32_2432s028
```

Luồng: layer check → build ESP-IDF trong `platform/esp32` → output tại `build/esp32_2432s028/`:

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
I (xxx) board: ESP32-2432S028R ready (320x240, touch=yes)
I (xxx) main: OS started on ESP32-2432S028R
```

Màn hình hiện Launcher với 4 icon: Stopwatch, Touch, System, Settings.

### Nạp thủ công bằng esptool (không cần build lại)

Dùng khi chỉ có sẵn thư mục build (ví dụ artifact từ CI):

```bash
cd build/esp32_2432s028
python3 -m esptool --chip esp32 -p /dev/ttyUSB0 -b 460800 write_flash @flash_args
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
| Màn hình trắng, đèn nền sáng | Sai cấu hình panel. Thử `invert = true` trong `DISPLAY` ở [board_config.hpp](../platform/esp32/boards/esp32_2432s028/board/src/board_config.hpp). |
| Hình bị lật / gương | Đổi `mirror_x` / `mirror_y` trong `DISPLAY`. |
| Chạm lệch / ngược trục | Mở app **Touch**, so tọa độ hiển thị với vị trí chạm; chỉnh `swap_xy`, `invert_x/y`, `raw_*_min/max` trong `TOUCH`. |
| Màn hình tối hẳn | Xem log: nếu có `Backlight PWM unavailable` thì GPIO21 bị chiếm/sai. |
| Reset liên tục, log có `abort()` từ `board` | Lỗi init LCD SPI – dòng `E (...) board: ... failed` cho biết bước nào. |
| Log `SD card unavailable`, app **Apps** báo `No SD card` | Thẻ chưa cắm, không phải FAT32, hoặc lỏng tiếp xúc. Cắm lại thẻ rồi khởi động lại. |
| App cài thêm hiện **App error** | Lỗi trong mã Lua của app: thông báo hiện ngay trên màn hình và trong log serial (`[<id>] script error: ...`). |
| Backtrace khi crash | `monitor` tự giải mã nhờ `gach_os.elf`; luôn mở monitor từ `./build.sh` để có symbol. |
