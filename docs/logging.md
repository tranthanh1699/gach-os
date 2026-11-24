# gach log: bộ ghi log của OS

Một API log dùng chung cho mọi tầng (`common` → `apps`, cả app Lua), cùng định dạng với log của ESP-IDF, có lịch sử
xem lại được bằng `gach dmesg` và đổi mức log lúc chạy bằng `gach loglevel`.

## 1. Dùng trong code C++

```cpp
#include "common/log.hpp"

namespace {
constexpr const char* TAG = "wifi";
}

LOGI(TAG, "connected to '%s', ip %s", ssid, ip);
LOGW(TAG, "retry %u/%u", attempt, MAX_RETRY);
LOGE(TAG, "connect failed (status=%u)", static_cast<unsigned>(status));
LOGD(TAG, "scan: %u network(s)", count);   // mặc định ẩn (mức INFO)
```

| Macro | Mức | Khi nào |
| :--- | :--- | :--- |
| `LOGE` | ERROR | Chức năng hỏng, người dùng thấy được hậu quả |
| `LOGW` | WARN | Bất thường nhưng hệ thống tự xử lý được (thử lại, bỏ qua gói lỗi...) |
| `LOGI` | INFO | Sự kiện quan trọng: mở/đóng app, kết nối mạng, cài app. **Mặc định hiển thị** |
| `LOGD` | DEBUG | Chi tiết cho người phát triển (URL request, số mạng quét được...) |
| `LOGV` | VERBOSE | Rất chi tiết, có thể nhiều dòng mỗi giây |

Quy ước:
- Tag ngắn, chữ thường, theo module: `app`, `fw`, `wifi`, `time`, `runtime`; app Lua dùng id của app.
- Không log trong ISR, không log mật khẩu / dữ liệu cá nhân.
- Không dùng `printf` trong `src/`. Log từ `platform/` vẫn có thể dùng `ESP_LOGx` (cùng đi một đường ra).
- Nội dung tối đa 200 ký tự (phần dư bị cắt); dòng không cần `\n` cuối.

Định dạng đầu ra (giống ESP-IDF): `I (18131) wifi: connected to 'Trang-2.4G', ip 192.168.1.7` (mức, ms từ lúc boot, tag).

## 2. Mức log

- Mặc định: INFO cho mọi tag.
- Lúc chạy: `gach loglevel d` (toàn cục), `gach loglevel wifi v` (riêng một tag, tối đa 16 tag), `gach loglevel` (xem).
  Mức: `n`/`none`, `e`/`error`, `w`/`warn`, `i`/`info`, `d`/`debug`, `v`/`verbose`. Mất khi khởi động lại.
- Trong code: `common::log::set_level(Level::DEBUG)`, `common::log::set_level("wifi", Level::VERBOSE)`.
- Lúc biên dịch: định nghĩa `GACH_LOG_MIN_LEVEL` (0..5, mặc định 5) để loại hẳn các mức chi tiết khỏi binary bản release.
- `gach loglevel` chỉ áp cho gach log; log ESP-IDF giữ mức của `CONFIG_LOG_DEFAULT_LEVEL`.

## 3. Xem log

| Cách | Thấy gì |
| :--- | :--- |
| `tools/gach logcat` | Như `adb logcat`: lịch sử (từ lúc boot, tối đa 32 KiB) rồi theo dõi log mới |
| `tools/gach logcat -f` | Chỉ log mới từ lúc kết nối |
| `tools/gach dmesg` | In lịch sử rồi thoát (dùng trong script) |
| `tools/gach log -l w 30` / `log filter <text>` | 30 dòng W/E cuối / dòng chứa chuỗi |
| `tools/gach log export <file>` | Lưu lịch sử ra file |
| `tools/gach event list` | Sự kiện hệ thống (`EVENT_x`, xem [diagnostics.md](diagnostics.md)) |
| `gach shell` → `reboot` → `logcat` | Cả ROM / bootloader / đoạn trước `app_main` (chưa có trong lịch sử) dưới dạng chữ thô |
| `./build.sh monitor` | Chữ thô trên UART (khi không có `gach` kết nối) |

## 4. Kiến trúc

```
LOGx(tag, ...)  ─►  common::log (lọc mức, format, không cấp phát)  ─►  backend do platform cắm
ESP_LOGx(...)   ─►  esp_log_set_vprintf hook ───────────────────────────────┐
                                                                             ▼
                              platform/esp32/main: emit_line(line)
                                ├─ runtime::log_history_append  (ring 32 KiB trong PSRAM -> gach dmesg)
                                └─ runtime::bridge_log (khung LOG khi gach kết nối) | fwrite(stdout)
```

- Lõi `src/common/include/common/log.hpp`: header-only, không phụ thuộc RTOS/SDK. Chưa có backend thì bỏ qua log.
- Backend: `platform/esp32/main/main.cpp` (`log_backend`, `log_vprintf`), cắm ngay đầu `app_main`.
- Port sang chip khác: viết lại backend (timestamp + đường ra); code trong `src/` không đổi.
