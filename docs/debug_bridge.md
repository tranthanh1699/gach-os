# Debug bridge (`tools/gach`)

Công cụ kiểu `adb` cho gach-os, chạy qua **chính cáp USB-serial dùng để nạp firmware**. Không cần rút thẻ SD
để cài app, không cần mạng.

## 1. Dùng nhanh

```bash
tools/gach devices                              # kiểm tra kết nối
tools/gach install appstore/dist/*.gapp         # đẩy gói vào /install trên thẻ rồi cài ngay
tools/gach apps                                 # app đã cài (id, version, tên)
tools/gach uninstall org.gachos.g2048
tools/gach logcat                               # như adb: log từ lúc boot rồi theo dõi tiếp (Ctrl-C để thoát)
tools/gach logcat -f                            # chỉ log mới
tools/gach dmesg                                # 32 KiB log gần nhất, kể cả lúc khởi động (xem docs/logging.md)
tools/gach loglevel wifi d                      # đổi mức log lúc chạy (toàn cục: loglevel d; xem: loglevel)
tools/gach shell                                # shell tương tác, gõ help
tools/gach push notes.txt apps/foo/notes.txt    # chép lên thẻ (đường dẫn tương đối gốc thẻ)
tools/gach pull apps/foo/data.ini -             # lấy file về (- = in ra stdout)
tools/gach ls -l apps | cat <file> | grep -r <text> apps | tree -L 2 | df | info | mem | reboot
tools/gach launch settings | home | back      # điều khiển app từ xa (test tự động)
tools/gach perf bench                           # đo vẽ lại toàn màn 20 lần (fps, thời gian khung)
tools/gach help                                 # lệnh theo nhóm; help apps / help heap: chi tiết
tools/gach app list                             # mọi app + trạng thái; app inspect|stop|kill|disable <id>...
tools/gach health                               # chấm điểm OK/WARN/BAD: RAM, stack task, CPU, nhiệt độ, thẻ, Wi-Fi, reset
tools/gach top                                  # %CPU từng task, làm mới mỗi giây (Ctrl-C)
tools/gach diagnose                             # health + crash + sổ lỗi + lỗi gần đây; xem docs/diagnostics.md
tools/gach trace start; tools/gach trace export ui.json   # trace thời gian -> ui.perfetto.dev
tools/gach crash dump                           # core dump lần crash gần nhất + backtrace
tools/gach store list | store install <id>      # kho trực tuyến (GitHub Pages); store url <url|default> đổi kho
tools/gach screenshot [scale] [file.png]        # chụp màn hình (scale 1-8, mặc định 4 = 320x200), ~16 s
tools/gach input tap 720 80                      # chạm giả (pixel màn 1280x800); press x y [ms] giữ lâu
tools/gach input swipe 900 400 300 400           # vuốt; drag x1 y1 x2 y2 [hold_ms] [ms] = giữ rồi kéo
# trong gach shell: uidiag – trạng thái task UI không cần khóa (vòng lặp, phase, đứng bao lâu) khi nghi UI treo
```

- Cổng mặc định `/dev/ttyUSB0`; đổi bằng `-p /dev/ttyACM0` hoặc biến `GACH_PORT`.
- Chỉ cần Python 3 (thư viện chuẩn, Linux/macOS). User cần thuộc group `dialout`.
- Mở cổng **không reset board**: tool không đụng DTR/RTS (mạch auto-reset không bị kích).
- Không chạy đồng thời với `idf.py monitor`/`./build.sh flash` trên cùng cổng (hai chương trình sẽ tranh dữ liệu).
- Tốc độ ~8 KB/s (115200 baud): gói 20 KB cài trong ~2,5 s.

## 2. Hành vi log

| Trạng thái | Log trên cổng serial |
| :--- | :--- |
| Không có máy tính kết nối | Chữ thô như bình thường – `idf.py monitor` đọc được |
| `gach` đang kết nối (gửi PING mỗi 1 s) | Đóng khung (kênh LOG), không lẫn với dữ liệu lệnh / file |
| `gach` thoát, im lặng > 5 s | Board tự quay về chữ thô |

Log trước khi bridge khởi động (ROM, bootloader, đầu app) và `printf` trực tiếp luôn ra dạng thô;
`gach logcat` hiển thị cả hai loại.

## 3. Giao thức

Khung trên dây: `0x00 | COBS(type | id | data | crc16_le) | 0x00`

- COBS đảm bảo trong khung không có byte `0x00` → byte `0x00` là ranh giới. Byte `0x00` ở **đầu** khung tách
  khung khỏi log thô đứng trước nó.
- CRC-16/CCITT-FALSE (poly `0x1021`, init `0xFFFF`) trên `type | id | data`. Sai CRC → bỏ khung.
- `data` tối đa 256 byte.

| type | Hướng | Ý nghĩa | data |
| :--- | :--- | :--- | :--- |
| `0x01` CMD | PC → board | Dòng lệnh | chữ, VD `ls apps` |
| `0x02` DATA | PC → board | Một đoạn file khi `push` | ≤ 256 byte |
| `0x03` ACK | PC → board | Xác nhận đoạn khi `pull` (id = id đoạn) | — |
| `0x04` PING | PC → board | Giữ phiên; board trả HELLO | — |
| `0x81` RESP | board → PC | Kết thúc một lệnh (id = id lệnh) | `status` (0 = OK) + chữ |
| `0x82` DATA | board → PC | Một đoạn file khi `pull`; đoạn rỗng = hết file | ≤ 256 byte |
| `0x83` ACK | board → PC | Xác nhận đoạn khi `push` | `status` |
| `0x84` LOG | board → PC | Đoạn log | chữ |
| `0x85` HELLO | board → PC | Trả lời PING | `gach-bridge <version> <board>` |
| `0x86` TEXT | board → PC | Một dòng kết quả của lệnh (trước RESP) | chữ |

Truyền file theo kiểu **dừng-và-chờ**: mỗi đoạn phải được ACK mới gửi đoạn sau (UART không mất dữ liệu âm thầm).

```
push:  PC  CMD "push install/a.gapp 20480"  →  board  RESP ok "ready"
       PC  DATA(id=n) 256 B                 →  board  ACK(id=n) ok        (lặp)
                                               board  RESP ok "done"      (đủ kích thước)
pull:  PC  CMD "pull apps/x/main.lua"       →  board  RESP ok "sending"
       board DATA(id=k)                     →  PC     ACK(id=k)           (lặp tới DATA rỗng)
```

## 4. Kiến trúc

| Tầng | Thành phần |
| :--- | :--- |
| HAL | `hal::IDebugPort` (đọc/ghi byte, ghi liền khối) – `Devices::p_debug_port` |
| Platform | `esp32_mcal::Esp32UartDebugPort`: driver UART console; `printf`/`ESP_LOG` cũng đi qua driver nên không xen byte vào giữa khung |
| Runtime | `runtime::bridge_start()` – task `gach_bridge` (ưu tiên dưới UI); lệnh cài/gỡ app và mở/đóng file giữ `fw::UiLock` |
| main | Hook `esp_log_set_vprintf` → `runtime::bridge_log()` (không có phiên → in chữ thô) |
| PC | `tools/gach` (Python, termios) – test: `python3 -m unittest tools/test_gach.py` |
