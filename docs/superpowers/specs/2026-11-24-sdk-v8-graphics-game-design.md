# SDK v8: ảnh, engine 2D `gach.game` và game mẫu thủ thành

Ngày: 2026-11-24 · Trạng thái: chờ Kỹ sư V duyệt

## 1. Mục đích

App Lua cài từ kho phải làm được những gì app C++/LVGL tích hợp làm được, về cả hình thức (ảnh thật, nhân vật đẹp)
lẫn hiệu năng (nhiều vật thể chuyển động mượt). Thước đo cụ thể: một game thủ thành theo làn viết hoàn toàn bằng Lua
chạy mượt trên JC8012P4A1C.

Lý do các giới hạn hiện tại là thiết kế ban đầu, không phải phần cứng:

- `ScriptHost::MAX_OBJECTS = 256`, `MAX_BINDINGS = 256`, `MAX_TIMERS = 8`: bảng cố định.
- Heap LVGL `CONFIG_LV_MEM_SIZE = 128 KiB` trong RAM nội là trần thật của số widget.
- SDK chỉ có `box`/`label`/widget chuẩn: không ảnh, không sprite, không canvas.
- Mỗi vật thể là một widget LVGL; Lua gọi C cho từng `set_pos`; mỗi `animate` là một animation LVGL riêng.

## 2. Mục tiêu / không làm / thất bại nếu

**Mục tiêu**

- G1. App hiển thị ảnh PNG đóng trong gói (nhân vật, icon, nền), có alpha.
- G2. Giới hạn widget / sự kiện / timer đủ rộng cho app phức tạp.
- G3. `gach.game`: scene 2D native, ≥ 500 sprite động ở ≥ 30 fps trên màn 1280x800 (vùng thay đổi cục bộ), Lua chỉ
  làm logic.
- G4. Game mẫu thủ thành theo làn, nguyên bản, chơi được trọn vẹn, đóng gói `.gapp`.
- G5. Hợp đồng SDK giữ nguyên: chỉ thêm API, `SDK_VERSION` 7 → 8, app v1–v7 chạy y như cũ.

**Không làm**

- Âm thanh (board chưa có đường audio trong HAL).
- Vật lý tổng quát (trọng lực, đàn hồi, xoay vật thể), tilemap, 3D, shader.
- Giải mã PNG trên board (xem quyết định D1).
- Nhiều app script chạy đồng thời.

**Thất bại nếu**

- App cũ (calculator, weather, pet, game2048...) chạy sai hoặc chậm đi.
- UI hệ thống (launcher, nav bar) chậm đi đo được bằng `gach perf bench`.
- Game giật (< 30 fps) khi màn đông vật thể, hoặc treo UI khi Lua chậm.
- Game giống Plants vs. Zombies về tên, nhân vật, hình ảnh, màn chơi. Chỉ dùng chung thể loại / cơ chế.

## 3. Ràng buộc

- Không cấp phát động trong `src/` (`new/malloc/std::vector`): mọi bảng cố định, bộ nhớ lớn do board cấp qua
  `hal::MemoryRegion`.
- LVGL chỉ gọi từ task UI. Callback Lua có ngân sách 500 ms (giữ nguyên).
- Màn 1280x800 RGB565, vẽ bằng CPU (LVGL SW, 1 draw unit; PPA và 2 draw unit đã đo là chậm hơn).
- PSRAM ~27 MiB trống; RAM nội ~100 KiB trống.
- Tool phía máy tính chỉ dùng thư viện chuẩn Python.
- Thêm trường vào `hal::Devices` = thay đổi tầng HAL → cần Kỹ sư V đồng ý (mục 6, D3).

## 4. Các hướng đã cân nhắc

| Hướng | Nội dung | Đánh đổi |
| :--- | :--- | :--- |
| 1 | Nâng giới hạn + `ui.image` + `ui.canvas` | Ít việc, lợi cho mọi app; game vẫn mỗi vật thể một widget → giật từ vài trăm vật thể |
| 2 | Chỉ `gach.game` | Hiệu năng game tốt; app giao diện không được gì |
| **3 (đã chọn)** | Bước 1 phần chung (ảnh, giới hạn), bước 2 `gach.game` dùng lại phần ảnh, bước 3 game mẫu | Việc lớn nhất; mỗi bước đo được trên board, cả app thường lẫn game đều lợi |

Kỹ sư V chọn hướng 3.

## 5. Thiết kế

### 5.1 Bước 1 – Ảnh và giới hạn (SDK v8 phần `gach.ui`)

**Định dạng ảnh trong gói.** `gapp_pack.py` đổi mọi `*.png` trong thư mục app sang `*.gimg` lúc đóng gói:

```
header 16 B: magic "GIMG", version u8, format u8 (0 = RGB565, 1 = RGB565A8), reserved u16, w u16, h u16,
             stride u16, reserved u16
data:        RGB565 little-endian (w*h*2) [+ alpha A8 (w*h)]
```

- RGB565A8 trùng định dạng `LV_COLOR_FORMAT_RGB565A8` của LVGL → board nạp thẳng vào bộ nhớ, dựng
  `lv_image_dsc_t`, không chuyển đổi.
- Ảnh không có alpha (hoặc alpha toàn 255) → RGB565 (nhỏ hơn, vẽ nhanh hơn).
- Giải PNG bằng `zlib` của Python (8-bit RGB/RGBA/gray/palette, không interlace; định dạng khác → lỗi đóng gói rõ ràng).
- Trong Lua vẫn gọi bằng tên `.png` (`ui.image(p, "img/hero.png")`): runtime tự tìm `.gimg`.

**Kho ảnh (asset cache).** Vùng PSRAM riêng cho app đang chạy (mặc định 8 MiB, D3), cấp kiểu bump allocator, thu hồi
toàn bộ khi app đóng (như arena Lua). Bảng tối đa 256 ảnh/app, tra theo đường dẫn; nạp lần đầu, dùng lại về sau.

**API mới `gach.ui` (manifest `sdk=8`)**

| API | Ý nghĩa |
| :--- | :--- |
| `ui.image(parent, path, {w=, h=, fit="contain"\|"cover"\|"fill", opa=}) -> h` | Widget ảnh |
| `ui.set_image(h, path)` | Đổi ảnh |
| `ui.image_info(path) -> w, h` | Kích thước gốc (dp) |
| `ui.canvas(parent, w, h) -> h` | Canvas vẽ tay, bộ đệm trong kho ảnh |
| `ui.draw(h, {cmd...})` | Danh sách lệnh vẽ một lần gọi: `{"fill", color}`, `{"rect", x,y,w,h, color, radius}`, `{"line", x1,y1,x2,y2, color, width}`, `{"circle", x,y,r, color}`, `{"image", path, x, y}`, `{"text", x, y, str, color, size}` |

Ảnh theo dp như mọi kích thước khác (co giãn 150 % trên board này). Phóng ảnh dùng `lv_image_set_scale`; khuyến nghị
đóng gói ảnh sẵn ở kích thước hiển thị để khỏi tốn CPU.

**Nâng giới hạn**

| Giới hạn | Cũ | Mới |
| :--- | :--- | :--- |
| Widget / app | 256 | 1024 |
| Cặp sự kiện `ui.on` | 256 | 1024 |
| Timer | 8 | 32 |

Heap LVGL: thử đặt `CONFIG_LV_MEM_SIZE` lớn trong PSRAM (pool tĩnh `LV_MEM_POOL` vào PSRAM, hoặc
`LV_USE_STDLIB_MALLOC=CLIB` với malloc ưu tiên PSRAM). **Chỉ giữ nếu `gach perf bench` không chậm quá 3 %** trên
launcher, weather, uikit; không thì giữ 128 KiB RAM nội và ghi lý do vào `sdkconfig.defaults` (như PPA / draw unit).

### 5.2 Bước 2 – Engine `gach.game`

**Mô hình.** Một scene native cho app đang chạy:

- Một đối tượng LVGL tự vẽ (`game.scene(parent, w, h)`) chiếm vùng chơi; widget `gach.ui` thường đặt chồng lên làm HUD.
- Bảng sprite cố định 1024 mục trong PSRAM. Sprite id là số nguyên (Lua không cần bảng handle).
- Mỗi tick của lv_timer (≈ 16 ms): C++ cập nhật vị trí theo vận tốc, khung animation, hết hạn, va chạm; gom vùng cũ +
  mới của sprite đổi để `lv_obj_invalidate_area` (chỉ vẽ lại vùng thay đổi).
- Vẽ: trong `LV_EVENT_DRAW_MAIN`, duyệt sprite theo lớp (0–7) rồi theo y, cắt theo vùng đang vẽ, `lv_draw_image`
  từng sprite (ảnh từ kho ảnh, khung = vùng con của atlas).
- Lua chỉ nhận `on_update(dt)` ở nhịp cố định (mặc định 30 Hz, chỉnh được 10–60) và các sự kiện gom theo lô.

**API (manifest `sdk=8`)**

| API | Ý nghĩa |
| :--- | :--- |
| `game.scene(parent, w, h, {bg=color\|path, fps=30}) -> scene` | Tạo vùng chơi (một app một scene) |
| `game.sheet(path, frame_w, frame_h) -> sheet` | Atlas: ảnh chia lưới khung |
| `game.sprite({sheet=, frame=, x=, y=, layer=, group=}) -> id` | Tạo sprite |
| `game.set(id, {x=, y=, vx=, vy=, frame=, layer=, visible=, opa=, flip=, group=})` | Đổi nhiều thuộc tính một lần gọi |
| `game.get(id) -> x, y` | Vị trí hiện tại (sau khi C++ đã di chuyển) |
| `game.play(id, {from=, to=, fps=, loop=true, done=fn})` | Animation khung chạy native |
| `game.move_to(id, x, y, ms, done)` | Di chuyển tuyến tính native |
| `game.life(id, ms)` | Tự xóa sau ms (đạn, hiệu ứng) |
| `game.remove(id)` / `game.clear()` | Xóa |
| `game.on_overlap(group_a, group_b, fn)` | C++ kiểm tra chồng hình chữ nhật mỗi tick; gọi `fn(a, b)` một lần cho mỗi cặp mới chạm |
| `game.on_touch(fn)` | `fn(phase, x, y)` với phase `"down"`/`"move"`/`"up"`, tọa độ trong scene |
| `game.pick(x, y, group) -> id \| nil` | Sprite trên cùng tại điểm |
| `game.on_update(fn)` | `fn(dt_ms)` mỗi nhịp logic |
| `game.pause(bool)` / `game.stats() -> {sprites=, fps=, update_ms=}` | Tạm dừng / số đo |

Va chạm theo nhóm (≤ 16 nhóm, mặt nạ bit) → độ phức tạp O(n·m) giữa nhóm có đăng ký, không phải mọi cặp. Mỗi tick
tối đa 64 lần gọi Lua cho va chạm; dư thì dời sang tick sau.

**Hiệu năng (mục tiêu đo bằng `game.stats` + `gach trace`)**

- Tick native ≤ 2 ms với 500 sprite.
- `on_update` của game mẫu ≤ 5 ms.
- Vẽ lại cục bộ: ≥ 30 fps với 100 sprite chuyển động cùng lúc trên vùng chơi toàn màn.
- Trace: `game/tick`, `game/lua_update`, `game/draw` để xem trong Perfetto.

### 5.3 Bước 3 – Game mẫu "Core Guard"

Thể loại thủ thành theo làn, nhân vật và chủ đề nguyên bản:

- Bối cảnh: robot bảo vệ lõi năng lượng của trạm khỏi đàn slime / bọ máy tràn từ phải sang.
- Sân: 5 làn × 9 ô. Tài nguyên: **tế bào năng lượng**, rơi định kỳ và do robot "Dynamo" tạo ra, chạm để nhặt.
- Robot (đặt vào ô, mỗi loại có thời gian hồi):
  - Dynamo: tạo năng lượng.
  - Spark Turret: bắn tia điện.
  - Frost Emitter: bắn làm chậm.
  - Bulwark: khối chắn máu trâu.
  - Pulse Mine: nổ khi bị chạm.
- Địch: Blob (thường), Shell Blob (giáp), Skitter (nhanh, máu ít), Brute (chậm, máu nhiều).
- 5 màn, độ khó tăng dần, đợt lớn cuối màn. Lõi có 3 điểm khiên chung cho mọi làn: mỗi địch chạm lõi trừ 1 điểm
  (địch đó tan), hết khiên là thua.
- Lưu tiến độ (màn đã mở) bằng `gach.storage`.
- Đồ họa: PNG nguyên bản do một script Python sinh (pixel-art vẽ bằng hình học, phong cách riêng); script nằm trong
  `appstore/apps/coreguard/tools/` để vẽ lại được.

### 5.4 Thay đổi theo tầng

| Tầng | Thay đổi |
| :--- | :--- |
| `hal_api` | `Devices::asset_arena` (`MemoryRegion`) – **cần duyệt (D3)** |
| `platform/esp32` board | Cấp asset arena 8 MiB PSRAM; thử nghiệm heap LVGL |
| `runtime` | `asset_cache.*` (nạp `.gimg`), `script_image.cpp` (ui.image/canvas), `game_engine.*` + `script_game.cpp` (gach.game), nâng hằng giới hạn, `SDK_VERSION = 8` |
| `tools` | `gapp_pack.py`: PNG → `.gimg` + test |
| `appstore` | `apps/coreguard`, cập nhật `apps/uikit` minh họa ảnh/canvas |
| `docs` | `sdk.md` (v8, giới hạn mới), `diagnostics.md` (trace game) |

## 6. Quyết định

| # | Quyết định | Ai |
| :--- | :--- | :--- |
| D1 | Đổi PNG → `.gimg` lúc đóng gói, không giải PNG trên board (nhanh, không cần heap cho decoder) | Claude đề xuất |
| D2 | Scene vẽ bằng một đối tượng LVGL tự vẽ, không phải mỗi sprite một widget | Claude đề xuất |
| D3 | Thêm `Devices::asset_arena` (8 MiB PSRAM) – thay đổi HAL | **Cần Kỹ sư V duyệt** |
| D4 | Heap LVGL sang PSRAM chỉ khi bench chậm ≤ 3 % | Claude đề xuất |
| D5 | Game nguyên bản "Core Guard", art sinh bằng script | Claude đề xuất, theo yêu cầu "làm game thể loại này" |

## 7. Để lại cho người làm

- Cấu trúc nội bộ của `game_engine` (sắp xếp lớp, gom vùng invalidate) miễn đạt mục tiêu đo ở 5.2.
- Bảng chỉ số cân bằng game (máu, sát thương, giá) – chỉnh khi chơi thử.
- Chi tiết pixel-art.

## 8. Thứ tự và kiểm chứng

1. Bước 1 → build, `gach perf bench` trước/sau, test `gapp_pack.py`, app uikit hiển thị ảnh/canvas trên board.
2. Bước 2 → app thử `bench_sprites` (500 sprite) đo `game.stats` + trace; mọi app cũ vẫn chạy.
3. Bước 3 → chơi thử trên board qua `gach`, đo fps khi đông địch.

Mỗi bước commit riêng khi Kỹ sư V yêu cầu.
