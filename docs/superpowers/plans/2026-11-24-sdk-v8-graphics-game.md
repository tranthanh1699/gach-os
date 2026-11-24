# SDK v8 (ảnh, gach.game, Core Guard) Implementation Plan

> **For agentic workers:** REQUIRED SUB-SKILL: Use superpowers:subagent-driven-development (recommended) or superpowers:executing-plans to implement this plan task-by-task. Steps use checkbox (`- [ ]`) syntax for tracking.

**Goal:** App Lua hiển thị ảnh PNG, có engine 2D native `gach.game`, và game mẫu thủ thành "Core Guard" chạy mượt trên JC8012P4A1C.

**Architecture:** PNG đổi sang `.gimg` (RGB565 / RGB565A8) lúc đóng gói; board nạp vào kho ảnh (asset arena PSRAM do board cấp qua `hal::Devices::asset_arena`). `gach.game` = logic thuần C++ (`GameWorld`, test được trên máy) + một đối tượng LVGL tự vẽ (`GameScene`) + binding Lua. Lua chỉ nhận `on_update(dt)` và sự kiện theo lô.

**Tech Stack:** C++17, ESP-IDF 6.1, LVGL 9.6, Lua 5.4 (third_party), Python 3 stdlib.

**Spec:** `docs/superpowers/specs/2026-11-24-sdk-v8-graphics-game-design.md`

## Global Constraints

- Không `new/delete/malloc`, `std::vector/string/function` trong `src/`; bảng cố định, bộ nhớ lớn qua `hal::MemoryRegion`.
- Build `-Wall -Wextra -Werror -Wshadow`, 0 warning; `./build.sh check` PASS sau mỗi task chạm `src/`/`platform/`.
- LVGL chỉ gọi trên task UI. Ngân sách mỗi lần gọi Lua 500 ms (không đổi).
- SDK: chỉ thêm API; `SDK_VERSION` 7 → 8; app manifest `sdk<=7` chạy y như cũ; API mới chỉ có khi `sdk>=8`.
- Giới hạn mới: widget 1024, cặp `ui.on` 1024, timer 32, ảnh 256/app, sprite 1024, nhóm va chạm 16, lớp 0–7, ≤ 64 callback va chạm/tick.
- Asset arena: 8 MiB PSRAM, thu hồi toàn bộ khi app đóng.
- Định dạng `.gimg`: header 16 B `"GIMG"`, version u8 = 1, format u8 (0 RGB565, 1 RGB565A8), u16 reserved, u16 w, u16 h, u16 stride (byte/dòng của phần màu = w*2), u16 reserved; rồi RGB565 LE (h*stride) [+ A8 (w*h)].
- Mục tiêu đo: tick native ≤ 2 ms @ 500 sprite; ≥ 30 fps @ 100 sprite động toàn màn; heap LVGL sang PSRAM chỉ khi `perf bench` chậm ≤ 3 %.
- Game nguyên bản "Core Guard" (id `org.gachos.coreguard`): không dùng tên, nhân vật, hình ảnh hay cơ chế đặc trưng của Plants vs. Zombies.
- Log bằng `LOGx`; trace `TRACE_*` cho `game/tick`, `game/lua_update`, `game/draw`.
- Commit chỉ khi Kỹ sư V yêu cầu; không ký tên AI.

## Review Focus

- PNG lạ (interlace, 16-bit, gray+alpha, palette có tRNS) → đóng gói báo lỗi rõ hoặc đổi đúng; không sinh `.gimg` hỏng. (Task 1)
- Ảnh thiếu / `.gimg` hỏng / kho ảnh đầy → lỗi Lua có thông điệp (`image not found: x.png`, `asset memory full`), không crash. (Task 3)
- App đóng giữa lúc scene đang chạy (Home, `app kill`) → timer scene hủy, không vẽ vào ảnh đã thu hồi. (Task 8)
- Lua xóa sprite trong callback va chạm / on_update → không truy cập sprite đã xóa trong cùng tick. (Task 6)
- App sdk ≤ 7 không thấy API mới và không đổi hành vi (giới hạn cũ vẫn chạy được, không lỗi). (Task 4)

---

### Task 1: Đóng gói PNG → `.gimg`

**Files:**
- Modify: `tools/gapp_pack.py`
- Create: `tools/test_gapp_pack.py`

**Interfaces:**
- Produces: `png_to_gimg(data: bytes) -> bytes`; `pack()` thay mỗi `*.png` bằng `*.gimg` cùng tên gốc (đường dẫn `img/a.png` → `img/a.gimg`).

- [ ] **Step 1: Test** `tools/test_gapp_pack.py` (tự tạo PNG bằng zlib, không cần file mẫu):
  - `test_rgba_to_rgb565a8`: PNG 2x1 RGBA `(255,0,0,255),(0,0,255,128)` → header format 1, w 2, h 1, stride 4; màu `0x00F8, 0x1F00` (LE bytes `00 F8 1F 00`), alpha `FF 80`.
  - `test_opaque_rgb_to_rgb565`: PNG RGB 1x1 trắng → format 0, data `FF FF`, độ dài 18.
  - `test_palette_with_trns`, `test_gray_alpha`: đổi đúng.
  - `test_interlaced_rejected`, `test_16bit_rejected`: `SystemExit` có tên file.
  - `test_pack_replaces_png`: thư mục tạm có `main.lua`, `manifest.ini`, `img/a.png` → tar chứa `img/a.gimg`, không có `.png`.
- [ ] **Step 2:** `python3 -m unittest tools/test_gapp_pack.py` → FAIL.
- [ ] **Step 3:** Hiện thực: giải chunk IHDR/PLTE/tRNS/IDAT, bỏ lọc 5 loại filter PNG, RGB565 = `((r>>3)<<11)|((g>>2)<<5)|(b>>3)`; alpha toàn 255 → format 0.
- [ ] **Step 4:** Test PASS; `./appstore/gapp.sh pack` vẫn đóng gói mọi app cũ.

### Task 2: HAL asset arena

**Files:**
- Modify: `src/hal_api/include/hal_api/board.hpp` (thêm `MemoryRegion asset_arena;` sau `app_arena`)
- Modify: `platform/esp32/boards/jc8012p4a1c/board/src/board_config.hpp` (`ASSET_ARENA_BYTES = 8U * 1024U * 1024U`), `board.cpp` (cấp PSRAM như app_arena), mọi board khác khởi tạo `{nullptr, 0U}`
- Modify: `src/runtime/src/script_host.hpp/.cpp` (`bind(fs, arena, assets, http)`), `src/runtime/src/runtime.cpp:263`

**Interfaces:**
- Produces: `hal::Devices::asset_arena`; `ScriptHost::bind(hal::IFileSystem*, const hal::MemoryRegion& arena, const hal::MemoryRegion& assets, hal::IHttpClient*)`.

- [ ] **Step 1:** Sửa, `./build.sh check` PASS, build mọi board (`./build.sh --list` rồi build từng board) 0 warning.
- [ ] **Step 2:** Flash, `gach meminfo`: PSRAM trống giảm ~8 MiB; log boot không có cảnh báo arena.

### Task 3: Kho ảnh (`AssetCache`)

**Files:**
- Create: `src/runtime/src/asset_cache.hpp`, `src/runtime/src/asset_cache.cpp`, thêm vào `src/runtime/CMakeLists.txt`
- Test: host test trong scratchpad (`g++ -std=c++17 -I src/... asset_cache.cpp test.cpp` với `IFileSystem` giả)

**Interfaces:**
- Produces:
  ```cpp
  class AssetCache : private common::NonCopyable {
  public:
      static constexpr size_t MAX_IMAGES = 256U;
      void reset(const hal::MemoryRegion& region);           // thu hồi toàn bộ
      common::Status load(hal::IFileSystem& fs, const char* p_app_dir, const char* p_path,
                          const lv_image_dsc_t*& p_out);       // p_path ".png" -> đọc ".gimg"; đã nạp thì trả lại
      void* alloc(size_t bytes, size_t align);                 // canvas buffer; nullptr nếu đầy
      size_t used() const; size_t capacity() const;
  };
  ```
  Lỗi: `ERR_NOT_FOUND` (không có file), `ERR_PARAM` (header sai / kích thước không khớp), `ERR_NO_MEM` (đầy / quá 256 ảnh).
- [ ] **Step 1: Test** host: nạp `.gimg` hợp lệ → `header.w/h/cf` đúng (`LV_COLOR_FORMAT_RGB565A8` / `RGB565`), `data_size` = h*stride (+w*h); nạp lần 2 trả cùng con trỏ; magic sai → `ERR_PARAM`; file thiếu → `ERR_NOT_FOUND`; arena 1 KiB nạp ảnh 2 KiB → `ERR_NO_MEM`; `reset` → `used()==0`.
- [ ] **Step 2:** FAIL → hiện thực (bump allocator căn 4 B, đường dẫn lưu tối đa 64 ký tự) → PASS.

### Task 4: Giới hạn mới, `SDK_VERSION = 8`, `ui.image`

**Files:**
- Modify: `src/runtime/src/script_host.hpp` (MAX_OBJECTS 1024, MAX_BINDINGS 1024, MAX_TIMERS 32; thành viên `AssetCache m_assets;`)
- Modify: `src/runtime/include/runtime/sdk_version.hpp` (8, chú thích v8)
- Create: `src/runtime/src/script_image.cpp` (struct `UiV8` friend của ScriptHost, như `UiV6`): `register_ui_v8(lua_State*)` gọi trong `script_host.cpp` cạnh `register_ui_v6` khi `m_manifest.sdk >= 8`
- Modify: `docs/sdk.md` (mục v8 ảnh, bảng giới hạn)

**Interfaces:**
- Consumes: `AssetCache::load` (Task 3).
- Produces Lua: `ui.image(parent, path, {w=, h=, fit=, opa=}) -> h`, `ui.set_image(h, path)`, `ui.image_info(path) -> w, h` (dp). Lỗi Lua: `image not found: <path>`, `bad image: <path>`, `asset memory full`.

- [ ] **Step 1:** App thử `appstore/apps/uikit` thêm trang "Image" (sdk=8, `img/sample.png` 128x128 có alpha) — đóng gói, cài bằng `gach install`.
- [ ] **Step 2:** Hiện thực; `m_assets.reset()` trong `start()` và `close_vm()`.
- [ ] **Step 3:** Build, flash, `gach app launch org.gachos.uikit` → ảnh hiện đúng alpha; app calculator/weather/pet/game2048 mở được (`gach app launch`, `gach log -l e` trống lỗi mới).

### Task 5: `ui.canvas` + `ui.draw`

**Files:**
- Modify: `src/runtime/src/script_image.cpp`, `docs/sdk.md`, trang uikit

**Interfaces:**
- Produces Lua: `ui.canvas(parent, w, h) -> h` (buffer RGB565 từ `AssetCache::alloc`), `ui.draw(h, cmds)` với lệnh `{"fill",c}`, `{"rect",x,y,w,h,c,radius}`, `{"line",x1,y1,x2,y2,c,width}`, `{"circle",x,y,r,c}`, `{"image",path,x,y}`, `{"text",x,y,str,c,size}`; lệnh lạ → lỗi `unknown draw command: <tên>`. Tọa độ dp.
- [ ] **Step 1:** Hiện thực bằng `lv_canvas_init_layer` / `lv_draw_*` / `lv_canvas_finish_layer` một lần cho cả danh sách.
- [ ] **Step 2:** Trang uikit vẽ biểu đồ + hình; kiểm trên board.

### Task 6: `GameWorld` (logic thuần, test trên máy)

**Files:**
- Create: `src/runtime/src/game_world.hpp`, `src/runtime/src/game_world.cpp` (không include LVGL/Lua)
- Test: host test scratchpad

**Interfaces:**
- Produces:
  ```cpp
  struct Rect { int32_t x, y, w, h; };
  struct Sprite { bool used, visible; uint8_t layer, group; int32_t sheet; uint16_t frame, anim_from, anim_to;
                  uint16_t anim_period_ms; bool anim_loop; float x, y, vx, vy; float tx, ty; uint32_t move_left_ms;
                  uint32_t life_ms; uint8_t opa; bool flip; uint16_t w, h; uint32_t anim_acc_ms; };
  class GameWorld {
  public:
      static constexpr size_t MAX_SPRITES = 1024U, MAX_GROUPS = 16U, MAX_EVENTS = 64U;
      void clear();
      int32_t add(const Sprite& s);            // id 1..MAX, -1 nếu đầy
      Sprite* get(int32_t id);                 // nullptr nếu không tồn tại
      void remove(int32_t id);                 // trì hoãn tới cuối tick nếu đang trong step()
      void watch_overlap(uint8_t a, uint8_t b, bool on);
      int32_t pick(int32_t x, int32_t y, int16_t group) const; // lớp cao nhất, -1 nếu không có
      // Tiến dt ms: vận tốc, move_to, anim khung, life; ghi sự kiện; gom vùng bẩn (cũ ∪ mới).
      void step(uint32_t dt_ms, void (*dirty)(const Rect&, void*), void* p_ctx);
      size_t take_events(Event* p_out, size_t max); // Event{kind: OVERLAP|ANIM_DONE|MOVE_DONE|EXPIRED, a, b}
      size_t count() const;
  };
  ```
- [ ] **Step 1: Test** host: vận tốc 100 px/s × 500 ms → x +50; `move_to` tới đích đúng thời gian + sự kiện MOVE_DONE; anim `from 0 to 3 fps 10 loop=false` sau 400 ms ở frame 3 + ANIM_DONE một lần; `life 100` → hết hạn + EXPIRED + id tự hủy; overlap nhóm 1–2 chỉ báo một lần khi bắt đầu chạm, báo lại sau khi rời ra và chạm lại; overlap nhóm không đăng ký → không báo; 70 cặp chạm cùng tick → 64 sự kiện, 6 còn lại tick sau; `remove` trong lúc lặp sự kiện không làm hỏng sprite khác; `pick` chọn lớp cao nhất; vùng bẩn của sprite di chuyển bao cả vị trí cũ và mới; sprite đứng yên không sinh vùng bẩn.
- [ ] **Step 2:** FAIL → hiện thực → PASS; đo trên máy 500 sprite + 2 cặp nhóm × 1000 tick để có số tham khảo.

### Task 7: `GameScene` (LVGL)

**Files:**
- Create: `src/runtime/src/game_scene.hpp`, `src/runtime/src/game_scene.cpp`

**Interfaces:**
- Consumes: `GameWorld` (Task 6), `AssetCache` (Task 3).
- Produces:
  ```cpp
  class GameScene {
  public:
      common::Status create(lv_obj_t* p_parent, int32_t w, int32_t h, uint32_t logic_hz); // lv_timer 16 ms
      void destroy();                                  // hủy timer + obj; an toàn khi gọi nhiều lần
      GameWorld& world();
      int32_t add_sheet(const lv_image_dsc_t* p_img, uint16_t frame_w, uint16_t frame_h); // ≤ 64 sheet
      void set_background(uint32_t color, const lv_image_dsc_t* p_img);
      void set_paused(bool paused);
      using LogicFn = void (*)(void* p_ctx, uint32_t dt_ms); using TouchFn = void (*)(void*, uint8_t phase, int32_t x, int32_t y);
      void set_callbacks(LogicFn logic, TouchFn touch, void* p_ctx);
      struct Stats { uint32_t sprites, fps, tick_us, logic_us, draw_us; }; void stats(Stats&) const;
  };
  ```
  Vẽ trong `LV_EVENT_DRAW_MAIN`: sắp chỉ số sprite theo (layer, y) bằng mảng chỉ số tĩnh, bỏ sprite ngoài `clip_area`, `lv_draw_image` với `lv_image_dsc_t` con (atlas: dịch `data` theo khung, `stride` của ảnh gốc). TRACE `game/tick`, `game/draw`.
- [ ] **Step 1:** Hiện thực; build sạch.

### Task 8: Binding `gach.game`

**Files:**
- Create: `src/runtime/src/script_game.cpp` (`register_game(lua_State*)` khi sdk ≥ 8, tạo bảng `gach.game`)
- Modify: `script_host.hpp/.cpp` (thành viên `GameScene m_scene;`, `destroy()` trong `pause()`→`set_paused(true)`, `close_vm()`→`destroy()`, `kill()`)
- Modify: `docs/sdk.md` (mục `gach.game`)

**Interfaces:**
- Produces Lua đúng bảng ở spec 5.2: `scene, sheet, sprite, set, get, play, move_to, life, remove, clear, on_overlap, on_touch, pick, on_update, pause, stats`. Callback Lua chạy qua `protected_call` (ngân sách 500 ms); lỗi → `fault()` như mọi callback. TRACE `game/lua_update`.
- [ ] **Step 1:** Hiện thực; `on_update` gọi ở `logic_hz` (10–60, mặc định 30) với `dt` thật.
- [ ] **Step 2:** App thử `appstore/apps/spritebench` (sdk=8): nút thêm 100 sprite (tối đa 1000) bay nảy trong scene; nhãn HUD hiện `game.stats()`.
- [ ] **Step 3:** Flash; đo 100/500/1000 sprite: `fps`, `tick_us` (≤ 2000 @ 500), `trace export`. Mở spritebench → Home → mở lại 5 lần, rồi `gach app kill` khi đang chạy: không crash (`gach crash info` = none).

### Task 9: Heap LVGL sang PSRAM (thí nghiệm)

**Files:**
- Modify: `platform/esp32/boards/jc8012p4a1c/sdkconfig.defaults` (+ ghi chú kết quả)

- [ ] **Step 1:** Đo gốc: `gach perf bench` trên launcher, weather, uikit (mỗi cái 3 lần).
- [ ] **Step 2:** Thử `CONFIG_LV_USE_STDLIB_MALLOC` = CLIB + `CONFIG_SPIRAM_MALLOC_ALWAYSINTERNAL` hợp lý (hoặc pool `LV_MEM_POOL` PSRAM), đo lại.
- [ ] **Step 3:** Chậm ≤ 3 % → giữ, ghi số; > 3 % → hoàn lại, ghi số và lý do vào `sdkconfig.defaults`.

### Task 10: Game "Core Guard"

**Files:**
- Create: `appstore/apps/coreguard/{manifest.ini, main.lua}`, `appstore/apps/coreguard/tools/make_art.py`, `appstore/apps/coreguard/img/*.png` (sinh bởi script)
- Modify: `tools/gapp_pack.py` EXCLUDE thêm thư mục `tools/` của app (không đóng vào gói)

**Interfaces:**
- Consumes: `gach.game` (Task 8), `ui.image` (Task 4), `gach.storage`.
- Nội dung theo spec 5.3: lưới 5×9; robot Dynamo / Spark Turret / Frost Emitter / Bulwark / Pulse Mine; địch Blob / Shell Blob / Skitter / Brute; tài nguyên tế bào năng lượng; lõi 3 điểm khiên; 5 màn; lưu màn đã mở.
- [ ] **Step 1:** `make_art.py` (stdlib, ghi PNG bằng zlib) vẽ sprite sheet pixel-art nguyên bản: robot, địch (khung đi + khung bị đánh), đạn, tế bào năng lượng, ô sân, lõi, icon thẻ chọn.
- [ ] **Step 2:** `main.lua`: màn chọn level → màn chơi (HUD: năng lượng, thẻ robot có hồi chiêu, khiên lõi) → thắng/thua. Logic trong `game.on_update`; đạn/địch là sprite có vận tốc; trúng đạn qua `game.on_overlap`; chạm ô để đặt robot qua `game.on_touch` + `game.pick`.
- [ ] **Step 3:** Đóng gói, cài, chơi thử trên board; `game.stats` fps ≥ 30 ở đợt cuối màn 5; `gach log -l e` không có lỗi script.

### Task 11: Tài liệu và hoàn tất

**Files:**
- Modify: `docs/sdk.md` (v8 đầy đủ, mục Tương thích, ví dụ coreguard/spritebench), `docs/diagnostics.md` (trace game), `appstore/README.md`

- [ ] **Step 1:** Cập nhật; `python3 -m unittest tools/test_gach.py tools/test_gapp_pack.py tools/test_layer_check.py` PASS; `./build.sh check` PASS; build 0 warning.
- [ ] **Step 2:** Báo cáo số đo cho Kỹ sư V; commit khi được yêu cầu (tách: tool, HAL/platform, runtime ảnh, runtime game, apps, docs).
