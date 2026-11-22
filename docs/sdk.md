# gach SDK v1: viết app cài thêm cho gach-os

App cài thêm là một **gói `.gapp`** chứa mã Lua 5.4, tương tự file `.jar` trên Nokia S60 hay `.apk` trên Android. Gói chạy trong máy ảo có sandbox, nên cùng một gói chạy được trên mọi board gach-os, bất kể chip hay kích thước màn hình.

## 1. Cấu trúc một app

```
my_app/
├── manifest.ini      # bắt buộc
├── main.lua          # entry (đổi được bằng khóa `entry`)
└── ...               # file khác (chỉ đọc bởi runtime ở bản SDK này)
```

### manifest.ini

```ini
id=org.example.hello      # bắt buộc. [a-z0-9._-], <= 31 ký tự. Duy nhất, cũng là tên thư mục cài.
name=Hello                # bắt buộc. <= 23 ký tự, hiển thị trên launcher.
version=1.0.0             # bắt buộc.
sdk=1                     # bắt buộc. Phiên bản SDK tối thiểu app cần.
entry=main.lua            # tùy chọn (mặc định main.lua).
icon=bell                 # tùy chọn. Xem bảng icon bên dưới.
color=#E3B341             # tùy chọn. Màu nền icon.
permissions=storage       # tùy chọn. Danh sách cách nhau bằng dấu phẩy.
```

Icon hỗ trợ: `audio video list settings home image play bell gps file edit envelope call keyboard charge folder tint eye wifi shuffle loop`.

### main.lua

`main.lua` phải **trả về một table** chứa các hàm vòng đời. Hàm nào không cần thì bỏ qua.

```lua
local app = {}

function app.on_create(root)   -- root: handle vùng hiển thị của app (cột flex có padding)
  gach.ui.label(root, "Hello gach-os!")
end

function app.on_resume() end    -- app lên foreground
function app.on_pause() end     -- app bị che / sắp đóng (timer tự tạm dừng)
function app.on_destroy() end   -- app đóng
function app.on_back() return false end  -- true = tự xử lý nút Back

return app
```

## 2. API

Mọi widget được tham chiếu bằng **handle số nguyên**. Handle trở nên vô hiệu khi widget bị xóa; gọi API với handle đã vô hiệu sẽ gây lỗi rõ ràng, không làm crash máy.

### gach.ui

| Hàm | Mô tả |
| :--- | :--- |
| `label(parent, text) -> h` | Nhãn chữ |
| `button(parent, text, on_click?) -> h` | Nút bấm. `on_click()` |
| `column(parent) -> h` / `row(parent) -> h` | Container xếp dọc / ngang (tự xuống dòng) |
| `list(parent) -> h` | Danh sách cuộn, chiếm phần còn lại |
| `list_add(list, text, on_click?) -> h` | Thêm mục vào danh sách |
| `slider(parent, min, max, value?, on_change?) -> h` | Thanh trượt. `on_change(value)` |
| `set_text(h, text)` | Đổi chữ (label, nút, mục list) |
| `set_font(h, "small"\|"medium"\|"large")` | Cỡ chữ 14/20/28 px |
| `set_color(h, 0xRRGGBB)` | Màu chữ |
| `clear(h)` | Xóa mọi widget con |
| `delete(h)` | Xóa widget (không xóa được root) |

### gach.timer

| Hàm | Mô tả |
| :--- | :--- |
| `every(ms, fn) -> id` | Gọi `fn()` định kỳ (>= 10 ms). Tối đa 8 timer |
| `cancel(id)` | Hủy timer |

Timer tự **tạm dừng khi app ở nền** và bị hủy khi app đóng.

### gach.sys

| Hàm | Mô tả |
| :--- | :--- |
| `uptime_ms() -> int` | Thời gian từ khi máy khởi động |
| `time() -> table\|nil` | `{year, month, day, hour, min, sec, wday, utc_offset}`: giờ địa phương theo múi giờ chọn trong Settings. Trả `nil` nếu chưa đồng bộ NTP (chưa có Wi-Fi). `wday`: 0 = Chủ nhật. `utc_offset`: số phút lệch so với UTC |

### gach.storage (cần `permissions=storage`)

| Hàm | Mô tả |
| :--- | :--- |
| `get(key) -> string\|nil` | Đọc giá trị |
| `set(key, value)` | Ghi chuỗi (key `[A-Za-z0-9_]` <= 24 ký tự, value <= 128 byte, tổng <= 1 KiB) |

Dữ liệu lưu trong `apps/<id>/data.ini` trên thẻ SD và **được giữ lại khi nâng cấp** app. Gỡ app thì dữ liệu bị xóa theo.

### gach.app, gach.sdk_version, print

| Hàm | Mô tả |
| :--- | :--- |
| `gach.app.exit()` | Đóng app (về màn hình trước) |
| `gach.sdk_version` | Số phiên bản SDK của máy |
| `print(...)` | Ghi log ra cổng serial (`./build.sh monitor`) |

Thư viện Lua có sẵn: `string`, `table`, `math`, `utf8`, `coroutine` và các hàm cơ bản. Sandbox **không có** `io`, `os`, `require`, `load`, `dofile`.

## 3. Giới hạn (board ESP32-2432S028R, không PSRAM)

| Tài nguyên | Giới hạn | Khi vượt |
| :--- | :--- | :--- |
| RAM máy ảo | 64 KiB/app (gồm cả runtime Lua) | Lỗi `not enough memory`, app hiện màn hình lỗi |
| Thời gian mỗi lần gọi (callback, vòng đời) | 500 ms | Lỗi `script timeout` |
| Widget | 96 / app | Lỗi `too many UI objects` |
| Timer | 8 / app | Lỗi `too many timers` |
| App cài đồng thời | 8 | Báo lỗi khi cài |
| App script mở cùng lúc | 1 (mở app khác sẽ đóng app trước) | |

Khi app gặp lỗi, chỉ app đó hiện màn hình **App error** kèm thông báo; OS và các app khác vẫn chạy bình thường. Nhấn Back để thoát.

Số học dùng số nguyên 32 bit và float 32 bit (`LUA_32BITS`), nên tránh các phép tính cần độ chính xác double.

## 4. Đóng gói và cài đặt

```bash
python3 tools/gapp_pack.py examples/apps/counter   # -> build/gapp/org.gachos.counter.gapp
```

Công cụ kiểm tra manifest, file entry, và cú pháp Lua (nếu máy có `luac`). Gói là file tar ustar không nén, nên có thể kiểm tra bằng `tar tvf`.

Cài lên máy:

1. Thẻ microSD định dạng **FAT32**. Chép file `.gapp` vào thư mục **`/install`** trên thẻ (tạo thư mục nếu chưa có).
2. Cắm thẻ vào board, rồi một trong hai cách:
   - Khởi động lại: gói được cài tự động lúc boot.
   - Mở app **Apps**, nhấn nút thẻ SD.
3. App mới xuất hiện trên launcher. Cài thành công thì file `.gapp` bị xóa khỏi `/install`.

Nâng cấp: chép bản mới (cùng `id`) vào `/install` rồi cài như trên; dữ liệu `gach.storage` được giữ. Gỡ cài đặt: app **Apps**, chạm vào app, chọn **Uninstall**.

## 5. Ví dụ

- [`examples/apps/counter`](../examples/apps/counter): UI, sự kiện, timer, `gach.storage`.
- [`examples/apps/world_clock`](../examples/apps/world_clock): danh sách, `gach.sys.time()`, `gach.app.exit()`.

## 6. Tương thích

- Runtime từ chối gói có `sdk` lớn hơn `gach.sdk_version` của máy.
- API đã phát hành **không bao giờ bị xóa hay đổi nghĩa**; SDK mới chỉ thêm API. App viết cho `sdk=1` chạy được trên mọi bản gach-os về sau.
