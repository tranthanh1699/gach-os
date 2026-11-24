# Quy trình làm việc: build, flash, gach shell

Tài liệu dùng hằng ngày cho người phát triển gach-os. Mọi lệnh chạy ở **gốc repo**.
Tham chiếu chi tiết: [build_and_flash.md](build_and_flash.md) (cài môi trường, sự cố) ·
[debug_bridge.md](debug_bridge.md) (giao thức gach) · [sdk.md](sdk.md) (viết app Lua).

---

## 1. Chuẩn bị một lần

```bash
sudo usermod -aG docker,dialout $USER   # chạy docker không cần sudo + quyền cổng serial
# đăng xuất / đăng nhập lại, kiểm tra:
groups | grep -E 'docker|dialout'
sudo apt remove brltty                  # Ubuntu: gói này hay chiếm cổng CH340 (/dev/ttyUSB0)
```

- Không cần cài ESP-IDF: `./build.sh` tự build trong Docker (image `gach-os-idf`, ESP-IDF **v6.1.1**) ở lần chạy đầu.
- Nếu đã cài ESP-IDF 6.1 trên máy (`idf.py` có trong PATH), `./build.sh` tự dùng bản cài đó.
- Cổng serial: cắm cáp, xem `ls /dev/ttyUSB* /dev/ttyACM*`. Đặt mặc định để khỏi gõ mỗi lần:

```bash
export PORT=/dev/ttyUSB0        # cho ./build.sh flash / monitor
export GACH_PORT=/dev/ttyUSB0   # cho tools/gach
```

## 2. Board hỗ trợ

```bash
./build.sh --list
```

| Board (`-b`) | Phần cứng |
| :--- | :--- |
| `jc8012p4a1c` (mặc định) | ESP32-P4 10.1" 1280x800 (ngang), cảm ứng GSL3680, Wi-Fi qua ESP32-C6, PSRAM 32 MB |

## 3. Build & flash firmware

| Việc | Lệnh |
| :--- | :--- |
| Build | `./build.sh` |
| Nạp | `./build.sh -p /dev/ttyUSB0 flash` |
| Nạp rồi xem log | `./build.sh -p /dev/ttyUSB0 flash-monitor` (thoát: `Ctrl + ]`) |
| Chỉ xem log | `./build.sh -p /dev/ttyUSB0 monitor` |
| Kiểm tra kiến trúc (nhanh, không cần ESP-IDF) | `./build.sh check` |
| Dung lượng theo component | `./build.sh size` |
| menuconfig (chỉ sửa `build/<board>/sdkconfig`) | `./build.sh menuconfig` |
| Xóa sạch build của board | `./build.sh fullclean` |
| Shell trong container ESP-IDF | `./build.sh shell` |

Ghi chú:
- `flash` tự build lại nếu code thay đổi. Output nằm ở `build/<board>/` (`gach_os.bin`, `gach_os.elf`, `flash_args`).
- **Sửa `sdkconfig.defaults`** (chung hoặc của board) không áp vào build cũ → `fullclean` rồi build lại.
- Mỗi lần build đều chạy layer check trước; vi phạm kiến trúc là build dừng.
- Build lần đầu tải component (LVGL, esp_hosted...) qua Internet; các lần sau dùng cache + ccache.
- Log khởi động đúng: `board: <tên board> ready (...)` rồi `main: OS started on <tên board>`.

Kiểm thử trên PC (không cần board):

```bash
python3 -m unittest tools/test_layer_check.py tools/test_gach.py
```

## 4. gach – shell kiểu adb qua cáp USB

`tools/gach` nói chuyện với firmware qua **chính cáp nạp**: cài app không cần rút thẻ SD, xem log, chép file,
chạy lệnh. Chỉ cần Python 3. Mở cổng **không làm board khởi động lại**.

> Không chạy `gach` cùng lúc với `./build.sh flash`/`monitor` trên cùng cổng. Đóng monitor trước.

### 4.1 Lệnh một dòng

```bash
tools/gach devices                               # board có trả lời không
tools/gach info                                  # board, chip, flash, phiên bản SDK, có thẻ SD không
tools/gach mem                                   # heap còn trống, thấp nhất, uptime
tools/gach apps                                  # app đã cài: id  version  tên
tools/gach install appstore/dist/org.gachos.g2048.gapp   # cài / nâng cấp app (giữ dữ liệu app)
tools/gach install appstore/dist/*.gapp          # cài nhiều gói một lần
tools/gach uninstall org.gachos.g2048            # gỡ app (xóa cả dữ liệu)
tools/gach logcat                                # như adb: in log từ lúc boot rồi theo dõi tiếp, Ctrl-C để thoát
tools/gach logcat -f                             # chỉ log mới (bỏ qua lịch sử)
tools/gach ls                                    # thư mục gốc thẻ SD
tools/gach ls apps/org.gachos.g2048
tools/gach pull apps/org.gachos.g2048/data.ini - # in file ra màn hình (- = stdout)
tools/gach pull apps/org.gachos.g2048/data.ini save.ini
tools/gach push save.ini apps/org.gachos.g2048/data.ini
tools/gach mkdir notes
tools/gach rm notes/old.txt
tools/gach reboot
tools/gach -p /dev/ttyACM0 info                  # chỉ định cổng khác
```

Đường dẫn là **tương đối gốc thẻ SD** (`apps/...`, `install/...`); dấu `/` ở đầu được bỏ qua.
Mã thoát khác 0 khi lệnh lỗi, nên dùng được trong script.

### 4.2 Shell tương tác

```bash
tools/gach shell
```

```
gach> help
  system  sysinfo version uptime cpuinfo coreinfo resetinfo bootinfo stats health reboot
  memory  meminfo heap memmap
  tasks   ps top taskinfo stack
  apps    app list|info|inspect|status|launch|stop|restart|kill|enable|disable|install|uninstall|permissions|logs
  files   cd pwd ls mkdir rm rmdir cp mv cat head tail stat touch du df find grep tree mount format push pull
  logs    logcat dmesg loglevel
  ui      home back perf uidiag
gach> help files                                           ← cú pháp + mô tả từng lệnh của nhóm
gach> apps
org.gachos.calculator   2.0.0   Calculator
org.gachos.g2048        1.0.1   2048
2 package(s)
gach> install appstore/dist/org.gachos.calculator.gapp     ← lệnh phía máy tính: đẩy file rồi cài
installed 1 package(s)
gach> pull apps/org.gachos.calculator/data.ini -
gach> logcat                                               ← Ctrl-C để quay lại shell
gach> exit
```

Trong shell, `install <file>`, `push <local> <remote>`, `pull <remote> [local]` và `logcat` chạy phía máy tính
(đọc/ghi file trên PC); các lệnh còn lại gửi thẳng xuống board.

### 4.3 Bảng lệnh

| Lệnh | Tác dụng |
| :--- | :--- |
| `devices` | Kiểm tra kết nối, in cổng + tên board |
| `info` | Board, chip, số core, MHz, flash, SDK, có thẻ SD |
| `mem` | `heap_free`, `heap_min`, `uptime_ms` |
| `apps` | Danh sách app cài thêm |
| `install <file.gapp>...` | Đẩy vào `/install` trên thẻ rồi cài. App đang có cùng `id` được nâng cấp, dữ liệu giữ nguyên |
| `uninstall <id>` | Gỡ app và dữ liệu của nó |
| `logcat [-f]` | Như `adb logcat`: in lịch sử (từ lúc boot, tối đa 32 KiB) rồi theo dõi log mới; `-f` = chỉ log mới. Gồm log hệ thống + `print()` / `gach.log` của app Lua |
| `dmesg` | In lại 32 KiB log gần nhất, kể cả lúc khởi động / lúc chưa cắm máy |
| `loglevel [tag] [n\|e\|w\|i\|d\|v]` | Xem / đổi mức log toàn cục hoặc theo tag (VD `loglevel org.gachos.weather d`) |
| `shell` | Shell tương tác |
| `cd [dir]` / `pwd` | Thư mục hiện tại (giữ trong phiên shell; `/` = gốc thẻ, `..`, `~`). Mọi lệnh file + `push`/`pull` hiểu đường dẫn tương đối |
| `ls [-l] [-a] [path]` / `tree [-L N] [-a] [path]` | Liệt kê (`-l`: kích thước + ngày sửa, `-a`: file ẩn) / cây thư mục |
| `mkdir [-p]` / `rm [-r] [-f]` / `rmdir` / `cp [-r]` / `mv` / `touch` | Tạo / xóa / chép / chuyển-đổi tên / tạo file (cp -r, rm -r chạy từng bước, không làm đơ UI) |
| `cat [-n]` / `head [-n N]` / `tail [-n N]` / `stat` | Đọc file chữ (cat tối đa 2000 dòng, file nhị phân thì từ chối) / thông tin file |
| `du [-s] [path]` / `df` / `mount` | Dung lượng thư mục / thẻ / loại FAT, thẻ SDHC/SDXC, bus, sector, cluster |
| `find [path] [-name '*.lua'] [-type f\|d]` / `grep [-i] [-n] [-r] <text> <path>` | Tìm file theo mẫu / tìm chữ trong file hoặc cả thư mục |
| `format --yes` | **Xóa sạch thẻ** (mọi app cài thêm + dữ liệu), tạo lại `apps/` `install/` rồi khởi động lại |
| `push <local> <remote>` / `pull <remote> [local\|-]` | Chép file lên / về |
| `reboot` | Khởi động lại board |
| `launch <app_id>` / `home` / `back` | Mở app / về màn hình chính / quay lại (điều khiển từ xa, test tự động) |
| `help [nhóm\|lệnh]` | Lệnh theo nhóm: system, memory, tasks, apps, files, logs, ui, legacy. `help apps` = cú pháp + mô tả từng lệnh của nhóm; `[pc]` = lệnh chạy phía máy tính |
| `app list` / `app info <id>` / `app inspect <id>` / `app status` | Mọi app (loại builtin / system / script, phiên bản, đang chạy / nền / dừng, bật / tắt) / metadata / thêm tài nguyên đang dùng (RAM Lua, widget, timer, file của app) / app foreground, back stack, máy ảo Lua |
| `app launch\|stop\|restart <id>` / `app kill <id>` | Mở / đóng / mở lại app / buộc đóng app Lua mà KHÔNG chạy code của nó (app treo, lỗi) |
| `app enable\|disable <id>` | Ẩn app khỏi launcher và chặn mở (giữ sau khi khởi động lại; không tắt được Home / Settings / Apps) |
| `app install <file.gapp>` / `app uninstall <id>` | Cài gói từ máy tính (hoặc đường dẫn trên thẻ) / gỡ app + dữ liệu (đang mở thì đóng trước) |
| `app permissions <id>` / `app logs <id> [n]` | Quyền của app / n dòng log cuối liên quan tới app (mặc định 40) |
| `sysinfo` / `version` | Chip, revision, MAC, flash/PSRAM, thiết bị / phiên bản firmware, ESP-IDF, LVGL, SDK app |
| `uptime` / `resetinfo` / `bootinfo` | Thời gian chạy, số lần khởi động (giữ qua reset mềm), lý do reset (+ gợi ý khi crash / watchdog / brownout), phân vùng đang chạy |
| `cpuinfo` / `coreinfo` | Kiến trúc, tần số, tải từng core, task ghim vào core nào |
| `meminfo` / `heap` / `memmap` | Tóm tắt bộ nhớ / từng vùng heap (khối dùng, khối trống, phân mảnh) / heap + arena Lua + bảng phân vùng flash |
| `ps` / `top [rows]` / `taskinfo <tên\|id>` / `stack` | Danh sách task (trạng thái, priority, core, stack trống thấp nhất, %CPU) / %CPU trong 1 s, làm mới liên tục / chi tiết một task / stack tăng dần (cảnh báo dưới 512 B) |
| `heap trace start [n]` / `stop` / `dump [n]` / `heap trace` | Ghi các khối heap được cấp mà chưa giải phóng (tìm rò rỉ): địa chỉ, kích thước. Nơi gọi (4 cấp, giải bằng `addr2line`) chỉ có khi build bật frame pointer – xem `sdkconfig.defaults` của board (tốn ~15 KB RAM nội, chậm ~8 %) |
| `stats` / `health` | Tổng hợp một màn / chấm điểm sức khỏe thiết bị (OK, INFO, WARN, BAD) |
| `perf [reset\|bench]` | Hiệu năng vẽ: fps, thời gian khung, phần flush. `bench` = vẽ lại toàn màn 20 lần, in trung bình |

### 4.4 Log hoạt động thế nào

- Không chạy `gach`: log ra dạng chữ như bình thường → `./build.sh monitor` đọc được.
- `gach` đang kết nối: log được đóng khung để không lẫn với dữ liệu lệnh/file. `gach logcat` hiển thị đúng.
- Thoát `gach` quá 5 giây: board tự quay về log dạng chữ.
- Log lúc khởi động (trước khi OS chạy) luôn là chữ thô; `logcat` vẫn hiển thị.
- Mọi log (kể cả khi không cắm máy) được giữ trong bộ đệm 32 KiB: `gach dmesg` để xem lại. Chi tiết API và mức log:
  [logging.md](logging.md).

## 5. Vòng phát triển app Lua

```bash
./appstore/gapp.sh new todo --name "Todo"        # tạo appstore/apps/todo từ template (sdk=3)
# ... sửa appstore/apps/todo/main.lua ...
./appstore/gapp.sh check todo                    # kiểm tra manifest + cú pháp
./appstore/gapp.sh pack todo                     # -> appstore/dist/<id>.gapp
tools/gach install appstore/dist/<id>.gapp       # cài lên board qua cáp (~2-3 s)
tools/gach logcat                                # xem print() và lỗi script của app
```

- Lỗi Lua hiện trên màn hình (**App error**) và trong log: `[<id>] script error: ...`.
- Dữ liệu `gach.storage` của app nằm ở `apps/<id>/data.ini` → `gach pull` để xem, `gach push` để nạp dữ liệu mẫu.
- Không có cáp? Cách cũ vẫn dùng được: chép `.gapp` vào `/install` trên thẻ SD rồi mở app **Apps** → nút thẻ SD
  (hoặc `./appstore/gapp.sh install /media/$USER/SDCARD <app>`).

## 6. Vòng phát triển firmware

```bash
./build.sh check                                         # sau mỗi thay đổi trong src/ hoặc platform/
./build.sh -p /dev/ttyUSB0 flash          # build + nạp
tools/gach logcat                                        # xem log (không reset board)
```

- Muốn xem log ngay từ lúc khởi động: dùng `flash-monitor` (monitor reset board khi mở).
- Đo hiệu năng giao diện trên board P4: Settings → Display & Brightness → **Performance Overlay**
  (FPS, CPU% ở góc dưới phải).
- Đo hiệu năng vẽ lại toàn màn sau thay đổi giao diện: `tools/gach perf bench` (xem mục 4.3).

## 7. Sự cố thường gặp

| Triệu chứng | Cách xử lý |
| :--- | :--- |
| `gach: no response from device` | Firmware cũ chưa có bridge → nạp lại; sai cổng → `-p`; monitor đang mở → đóng |
| `gach: cannot open /dev/ttyUSB0: Permission denied` | Chưa thuộc group `dialout` (mục 1) |
| Flash báo `multiple access on port` / `device reports readiness to read but returned no data` | Tiến trình khác giữ cổng (monitor, gach, ModemManager) → đóng rồi nạp lại |
| `Không thấy cổng /dev/ttyUSB0` | Cáp chỉ sạc, `brltty` chiếm cổng, hoặc board chưa cấp nguồn |
| Đổi `sdkconfig.defaults` mà không có tác dụng | `./build.sh -b <board> fullclean` rồi build lại |
| `gach install` báo `no sd card` | Thẻ chưa cắm hoặc không phải FAT32 → cắm thẻ, `gach reboot` |
| App báo **App error** khi chạy | Xem `gach logcat` để biết dòng lỗi trong `main.lua` |
