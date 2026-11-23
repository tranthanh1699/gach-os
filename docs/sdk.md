# gach SDK (v4): viết app cài thêm cho gach-os

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

#### Bố cục & kiểu dáng (SDK v2, manifest `sdk=2`)

| Hàm | Mô tả |
| :--- | :--- |
| `box(parent) -> h` | Khối trống không nhận chạm: dùng làm hình trang trí (chấm, gạch, khung) |
| `set_size(h, w?, h?)` | Kích thước: số pixel, `"N%"` (theo cha) hoặc `"content"`; `nil` = giữ nguyên |
| `set_grow(h, n)` | Chiếm phần trống còn lại trong `row`/`column` theo tỷ lệ `n` |
| `set_bg(h, 0xRRGGBB)` | Màu nền (kiểu phẳng, không đổ bóng) |
| `set_radius(h, px\|"round")` | Bo góc; `"round"` = bo tròn tối đa (nút tròn / viên thuốc) |
| `set_align(h, "left"\|"center"\|"right")` | Căn lề chữ |
| `set_pad(h, px)` / `set_gap(h, px)` | Lề trong / khoảng cách giữa các phần tử con |
| `center(h)` | Căn giữa trong widget cha (VD: ký hiệu vẽ tay trong nút) |
| `SYMBOL.<TÊN>` | Ký hiệu có sẵn trong font: `PLUS MINUS CLOSE OK LEFT RIGHT UP DOWN HOME SETTINGS REFRESH PLAY PAUSE STOP BELL WIFI TRASH EDIT BACKSPACE WARNING` |

Font hệ thống chỉ có ASCII cùng các ký hiệu trong `SYMBOL`; ký tự khác (VD: `÷`, chữ có dấu) sẽ hiện ô trống. Ký hiệu thiếu có thể vẽ bằng `box` (xem nút chia trong app Calculator).

`column`/`row` chỉ dùng để bố cục, không nhận chạm (chạm rơi xuống widget chứa nó).

#### Co giãn theo màn hình (SDK v3, manifest `sdk=3`)

Gói app chạy được trên mọi kích thước màn hình. App khai báo `sdk=3` viết kích thước theo **dp**
(đơn vị thiết kế, bằng 1 pixel trên màn 320x240) và được hệ thống đổi sang pixel thật theo tỷ lệ màn hình (máy tính bảng 1280x800: 1.5x):

- Mọi số pixel truyền vào `set_size`, `set_pad`, `set_gap`, `set_radius` được hiểu là dp.
- `set_font("small"|"medium"|"large")` cho cỡ chữ co giãn tương ứng (14/20/28 dp).
- Root của app có lề chuẩn đã co giãn.

| Hàm | Mô tả |
| :--- | :--- |
| `screen_size() -> w, h` | Kích thước vùng nội dung của app (dp với `sdk=3`, pixel với `sdk<=2`): dùng để chọn bố cục (VD: thêm cột trên màn rộng) |
| `on_swipe(h, fn)` | Gọi `fn(dir)` với `dir` = `"up"`/`"down"`/`"left"`/`"right"` mỗi lần vuốt trên widget `h`. Widget được bật nhận chạm và tắt cuộn. Vuốt bắt đầu trên widget con không có callback (`box`, `label`, `row`, `column`) cũng được tính, nên thường gắn vào `root`. Mỗi widget chỉ có một callback: không dùng cho widget đã có `on_click`/`on_change` |

App `sdk<=2` **không đổi hành vi**: số pixel giữ nguyên nghĩa pixel thật, font đúng 14/20/28 px, nên trên màn lớn
sẽ hiển thị nhỏ nhưng bố cục không vỡ. Muốn co giãn, nâng manifest lên `sdk=3` (yêu cầu gach-os có SDK v3).

#### Widget số liệu (SDK v4, manifest `sdk=4`)

Widget chỉ để hiển thị (không nhận chạm), kiểu phẳng tối: rãnh `0x2C2C2E`, màu chỉ thị mặc định `0x0A84FF`.
Kích thước tính theo dp như SDK v3.

| Hàm | Mô tả |
| :--- | :--- |
| `bar(parent, min, max, value?) -> h` | Thanh ngang bo tròn, rộng 100%, cao 8 dp |
| `arc(parent, min, max, width?) -> h` | Cung tròn 270° (hở phía dưới), 120x120 dp, nét `width` dp (mặc định 10). Đặt `label` làm con rồi `center()` để hiện số ở giữa |
| `chart(parent, points, min, max) -> h` | Biểu đồ đường cuộn: `points` điểm (2..120), trục Y `min..max`, rộng 100% cao 120 dp, 3 đường lưới ngang |
| `chart_series(chart, 0xRRGGBB?) -> n` | Thêm một đường (tối đa 4), trả số thứ tự `n` (1, 2...) |
| `chart_push(chart, n, value)` | Thêm điểm mới vào bên phải đường `n`, các điểm cũ dịch sang trái. Chưa đủ điểm thì phần bên trái để trống |
| `set_value(h, value)` | Đổi giá trị `bar` / `arc` / `slider` |
| `set_accent(h, 0xRRGGBB)` | Màu phần chỉ thị của `bar` / `arc` / `slider` (VD: đổi sang đỏ khi vượt ngưỡng) |

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
| `stats() -> table` | Chỉ số vận hành (SDK v4), xem bảng dưới. Gọi khoảng 1 lần/giây |
| `time() -> table\|nil` | `{year, month, day, hour, min, sec, wday, utc_offset}`: giờ địa phương theo múi giờ chọn trong Settings. Trả `nil` nếu chưa đồng bộ NTP (chưa có Wi-Fi). `wday`: 0 = Chủ nhật. `utc_offset`: số phút lệch so với UTC |

`stats()` trả về bảng sau. Trường nào board không hỗ trợ thì **là `nil`**, nên luôn kiểm tra trước khi dùng.
Kích thước tính bằng byte; số vượt 2^31 (VD: dung lượng thẻ SD) là số thực.

| Trường | Nội dung |
| :--- | :--- |
| `chip`, `board` | Tên chip (VD `"esp32p4"`), tên board |
| `uptime_ms` | Thời gian từ khi khởi động |
| `cpu` | `{cores, mhz, tasks?, load?}`. `load[i]` = % tải core `i` (1 = core 0) tính trên khoảng giữa hai lần gọi `stats()` (lần đầu: trung bình từ lúc khởi động). `tasks` = số task RTOS |
| `ram` | Heap RAM nội: `{total, free, min, largest}`. `min` = thấp nhất từng ghi nhận, `largest` = khối liền lớn nhất còn cấp được |
| `psram` | `{total, free, min}`; `nil` nếu board không có PSRAM |
| `lvgl` | Heap giao diện: `{total, free, used_pct, frag_pct}` |
| `flash` | `{total, app_size, app_used}`: chip flash, phân vùng firmware, kích thước firmware |
| `sd` | Thẻ nhớ `{total, free}`; `nil` nếu không có thẻ. Lần gọi đầu có thể chậm (FAT đếm cụm trống), không tính vào giới hạn 500 ms |
| `temp` | Nhiệt độ chip (°C, số thực) |
| `fps` | Số lần màn hình vẽ lại mỗi giây kể từ lần gọi trước (màn hình đứng yên thì gần 0) |
| `wifi` | `{connected, ssid, ip?, rssi?}` (`rssi` dBm); `nil` nếu board không có Wi-Fi |

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

## 3. Giới hạn (board JC8012P4A1C)

| Tài nguyên | Giới hạn | Khi vượt |
| :--- | :--- | :--- |
| RAM máy ảo | 1 MiB/app trong PSRAM (gồm cả runtime Lua) | Lỗi `not enough memory`, app hiện màn hình lỗi |
| Thời gian mỗi lần gọi (callback, vòng đời) | 500 ms | Lỗi `script timeout` |
| Widget | 96 / app | Lỗi `too many UI objects` |
| Timer | 8 / app | Lỗi `too many timers` |
| App cài đồng thời | 32 | Báo lỗi `too many apps installed`, gói được giữ lại trong `/install` |
| App script mở cùng lúc | 1 (mở app khác sẽ đóng app trước) | |

Khi app gặp lỗi, chỉ app đó hiện màn hình **App error** kèm thông báo; OS và các app khác vẫn chạy bình thường. Nhấn Back để thoát.

Số nguyên là **32 bit** (phép toán số nguyên tràn sẽ quay vòng: muốn tính số lớn, đổi sang số thực bằng `x * 1.0`). Số thực là **double** (~15 chữ số có nghĩa).

## 4. Đóng gói và cài đặt

```bash
./appstore/gapp.sh new my_app --name "My App"     # tạo app mới trong appstore/apps/my_app
./appstore/gapp.sh pack my_app                       # -> appstore/dist/<id>.gapp
./appstore/gapp.sh install /media/$USER/SDCARD my_app  # đóng gói + chép vào <SD>/install/
```

Công cụ kiểm tra manifest, file entry, và cú pháp Lua (nếu máy có `luac`). Chi tiết quy trình: [appstore/README.md](../appstore/README.md). Gói là file tar ustar không nén, nên có thể kiểm tra bằng `tar tvf`.

Cài lên máy:

1. Thẻ microSD định dạng **FAT32**. Chép file `.gapp` vào thư mục **`/install`** trên thẻ (tạo thư mục nếu chưa có).
2. Cắm thẻ vào board, rồi một trong hai cách:
   - Khởi động lại: gói được cài tự động lúc boot.
   - Mở app **Apps**, nhấn nút thẻ SD.
3. App mới xuất hiện trên launcher. Cài thành công thì file `.gapp` bị xóa khỏi `/install`.

Nâng cấp: chép bản mới (cùng `id`) vào `/install` rồi cài như trên; dữ liệu `gach.storage` được giữ. Gỡ cài đặt: app **Apps**, chạm vào app, chọn **Uninstall**.

## 5. Ví dụ

- [`appstore/apps/counter`](../appstore/apps/counter): UI, sự kiện, timer, `gach.storage`.
- [`appstore/apps/world_clock`](../appstore/apps/world_clock): danh sách, `gach.sys.time()`, `gach.app.exit()`.
- [`appstore/apps/game2048`](../appstore/apps/game2048) (SDK v3): game 2048 với `on_swipe`, bàn cờ vuông co giãn theo màn hình, lưu ván đang chơi và điểm cao nhất.
- [`appstore/apps/sysmon`](../appstore/apps/sysmon) (SDK v4): System Monitor. Dashboard tải CPU từng core, RAM/PSRAM/heap LVGL, firmware/thẻ SD, nhiệt độ, Wi-Fi, cập nhật mỗi giây bằng `gach.sys.stats()` cùng `arc`, `bar`, `chart`.
- [`appstore/apps/calculator`](../appstore/apps/calculator) (SDK v3): máy tính kiểu điện thoại / máy tính bảng. Dùng `screen_size()` để chọn bố cục (màn rộng có thêm phím khoa học và lịch sử), lưu lịch sử bằng `gach.storage` dạng bộ đệm vòng, và vẽ ký hiệu `÷` bằng `box`.

## 6. Tương thích

| SDK | Thêm mới |
| :--- | :--- |
| 1 | `gach.ui` cơ bản, `timer`, `sys`, `storage`, `app` |
| 2 | Bố cục & kiểu dáng: `box`, `set_size`, `set_grow`, `set_bg`, `set_radius`, `set_align`, `set_pad`, `set_gap`, `center`, `SYMBOL` |
| 3 | Co giãn theo màn hình: app `sdk=3` dùng dp và font co giãn; `screen_size`, `on_swipe` (vuốt) |
| 4 | `sys.stats()` (CPU, bộ nhớ, lưu trữ, nhiệt độ, Wi-Fi); widget `bar`, `arc`, `chart` + `chart_series`/`chart_push`, `set_value`, `set_accent` |

- Runtime từ chối gói có `sdk` lớn hơn `gach.sdk_version` của máy.
- API đã phát hành **không bao giờ bị xóa hay đổi nghĩa**; SDK mới chỉ thêm API. App viết cho `sdk=1` chạy được trên mọi bản gach-os về sau.
