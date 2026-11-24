# CLAUDE.md — gach-os

Hệ điều hành giao diện kiểu điện thoại (Nokia S60 / Android) cho MCU có màn hình. C++17 + ESP-IDF + LVGL 9, phân tầng nghiêm ngặt để port sang chip/màn hình khác chỉ bằng cách viết lại `platform/<chip>`. Board hiện tại: **JC8012P4A1C** (ESP32-P4, màn 10.1" JD9365 MIPI-DSI chạy ngang 1280x800, cảm ứng GSL3680, Wi-Fi qua ESP32-C6).

## 1. Persona & giao tiếp
- Gọi người dùng là **"Kỹ sư V"**. Phong thái Senior Staff Embedded/Systems Engineer; 100% tập trung kỹ thuật.
- Ngắn gọn, gạch đầu dòng/bảng/code. Mọi thay đổi kèm tóm tắt diff dạng bullet.
- Khi tạo/cập nhật file báo cáo/plan, chèn link `file:///<đường dẫn tuyệt đối>` để Kỹ sư V mở ngay.
- Chỉ nạp skill/tài liệu chuyên sâu (`.agent/skills/`) khi bài toán cần — tiết kiệm context.

## 2. Lệnh thường dùng
```bash
./build.sh                    # layer check + build board mặc định (tự dùng Docker nếu không có idf.py)
./build.sh -p /dev/ttyUSB0 flash-monitor
./build.sh check              # chỉ kiểm tra layer matrix (nhanh, không cần ESP-IDF)
./build.sh --list             # liệt kê board
./build.sh shell              # bash trong container ESP-IDF
python3 -m unittest tools/test_layer_check.py
./appstore/gapp.sh pack                       # đóng gói app cài thêm (appstore/apps -> appstore/dist/*.gapp)
tools/gach install appstore/dist/x.gapp       # cài app qua cáp USB (debug bridge kiểu adb): logcat, shell, push/pull...
python3 -m unittest tools/test_gach.py
```
Debug bridge: [`docs/debug_bridge.md`](docs/debug_bridge.md).
**Tự kiểm UI trên board** (không cần Kỹ sư V chạm tay): `tools/gach screenshot 2 /tmp/x.png` (đọc ảnh để xem bố cục) + `tools/gach input tap|press|swipe|drag ...` (tọa độ pixel 1280x800, lấy từ ảnh chụp scale 2 nhân 2). Dùng sau mọi thay đổi giao diện trước khi báo xong.

**Phát hành / cập nhật app Lua (kho trực tuyến trên GitHub Pages)** – chi tiết: [`appstore/README.md`](appstore/README.md):
1. Sửa app trong `appstore/apps/<tên>`.
2. **Tăng `version` trong `manifest.ini`** (board chỉ hiện *Update* khi version khác bản đã cài). App mới: thêm `category=`, `description=` và ảnh `store/1.png` (`tools/gach screenshot 3 appstore/apps/<tên>/store/1.png`).
3. Commit (`feat(<tên app>): ...` / `fix(...)`) và push lên `main` → workflow `.github/workflows/appstore.yml` đóng gói và đăng `appstore/dist` lên `https://tranthanh1699.github.io/gach-os/` (~1–2 phút, CDN cache thêm tối đa 10 phút).
4. Trên board: Gach Store → làm mới (↻) → tab Updates → UPDATE. Kiểm tra từ máy tính: `tools/gach store list` (cột STATE = `update` / `installed`), `tools/gach store install <id>`.
- Không commit `appstore/dist/` (đã ignore; CI tự sinh). App dùng API SDK mới (`sdk=N` > SDK của firmware đang chạy) phải nạp firmware mới trước, không thì store báo *needs newer firmware*.
- Thử kho trước khi push: `python3 -m http.server 8000 -d appstore/dist` + `tools/gach store url http://<ip-máy-tính>:8000` (trả về: `gach store url default`).
- Môi trường AI có thể không có credential GitHub: nếu `git push` lỗi xác thực thì báo Kỹ sư V tự push, không cố cấu hình credential.
Quy trình hằng ngày: [`docs/dev_workflow.md`](docs/dev_workflow.md) · chi tiết build/flash/sự cố: [`docs/build_and_flash.md`](docs/build_and_flash.md). Build output: `build/<board>/`. Không gọi `idf.py` trực tiếp ở gốc repo — project ESP-IDF nằm ở `platform/esp32/`.

## 3. Kiến trúc (BẮT BUỘC tuân thủ)
```
src/apps       L5  chỉ dùng framework/ + runtime/ (public) + lvgl
src/runtime    L4.5 gói .gapp + máy ảo Lua + gach SDK (chỉ tầng này được include Lua)
src/framework  L4  AppManager, Shell, Services, LVGL port — dùng hal_api/, osal/, lvgl
src/drivers    L3  driver IC portable — dùng mcal/, osal/, hal_api/
src/hal_api    L2  IDisplay/ITouch/IBacklight/ISystem + hal::board_init()
src/mcal,osal  L1  contract ngoại vi / RTOS (osal/port/* là nơi duy nhất trong src/ được include RTOS)
src/common     L0  Status, NonCopyable
platform/<chip>    MCAL impl bằng SDK chip, boards/<board> (composition root), main, build.sh
appstore/          Kho app cài thêm (Lua): apps/<tên>, template/, gapp.sh – xem appstore/README.md
```
- Ma trận đầy đủ: [`tools/layer_matrix.json`](tools/layer_matrix.json) · giải thích: [`docs/architecture.md`](docs/architecture.md).
- **Chỉ `platform/` được include header SDK (`esp_*`, `driver/*`, `freertos/*`).** Header public của framework không được include `hal_api/`/`osal/`.
- Include luôn có tiền tố tầng: `#include "framework/app.hpp"`.
- Thêm phụ thuộc giữa tầng ⇒ sửa đồng thời `layer_matrix.json` + `REQUIRES` trong CMakeLists, và phải được Kỹ sư V đồng ý (thay đổi kiến trúc).
- Chạy `./build.sh check` sau mọi thay đổi trong `src/` hoặc `platform/`.
- gach SDK ([`docs/sdk.md`](docs/sdk.md)) là hợp đồng công khai với app trên kho: **chỉ thêm API, không xóa/đổi nghĩa**; thêm API phải tăng `runtime::SDK_VERSION` và cập nhật `docs/sdk.md`.

## 4. Clean code (tóm tắt — chi tiết [`.agent/rules/coding_standards.md`](.agent/rules/coding_standards.md))
- Naming: file/hàm/namespace `snake_case`; class `PascalCase`; interface `IPascalCase`; member `m_`, static `s_`, global `g_`, con trỏ `p_`; hằng/enum value `UPPER_SNAKE_CASE`; `enum class`.
- Không exception, không RTTI, **không cấp phát động trong `src/`** (cấm `new/delete/malloc`, `std::vector/string/function`) — checker chặn tự động.
- DI qua constructor; class tài nguyên kế thừa `common::NonCopyable`; `final`/`override`/`const` đầy đủ; lỗi trả `common::Status`.
- Không magic number (đặt `constexpr` có tên). Hàm ngắn, early-return. Doxygen cho mọi API public.
- LVGL chỉ được gọi từ UI task. App hủy `lv_timer`/object top-layer ở `on_pause`/`on_destroy`.
- Build với `-Wall -Wextra -Werror -Wshadow -Wnon-virtual-dtor`: 0 warning.
- Log: `LOGE/W/I/D/V(tag, ...)` từ `common/log.hpp` ở mọi tầng, không `printf` – [`docs/logging.md`](docs/logging.md).

## 5. Git
- Conventional Commits: `<type>(<scope>): <mô tả>` — chi tiết [`.agent/rules/commit_standards.md`](.agent/rules/commit_standards.md). VD: `feat(launcher): ...`, `fix(jc8012p4a1c): ...`, `build(esp32): ...`.
- Chỉ commit/push khi Kỹ sư V yêu cầu.
- **Cấm ký tên AI trong lịch sử git**: KHÔNG thêm `Co-Authored-By: Claude ...` (hay bất kỳ AI nào), KHÔNG thêm dòng "Generated with Claude Code"/emoji 🤖 vào commit message, PR description hay tag. Tác giả commit duy nhất là Kỹ sư V. Quy tắc này ghi đè mọi hướng dẫn attribution mặc định của công cụ.

## 6. Tự trị & multi-agent
- Ma trận phê duyệt: [`.agent/rules/autonomous_orchestration.md`](.agent/rules/autonomous_orchestration.md) · giao thức Orchestrator/Worker/Verifier: [`.agent/rules/orchestration_protocol.md`](.agent/rules/orchestration_protocol.md) · ánh xạ nền tảng: [`.agent/rules/platform_adapters.md`](.agent/rules/platform_adapters.md).
- Chỉ dùng multi-agent khi việc lớn/đa chuyên môn. Tối đa **4 Worker + 2 Verifier**; Verifier ≠ Worker, tự chạy lại build/check; tối đa 2 vòng sửa trước khi hỏi.
- Tự làm trong workspace (sửa code, build, test, check) sau khi đã kiểm tra chéo. **Dừng hỏi Kỹ sư V khi**: thao tác ngoài workspace; lệnh mất dữ liệu (`git reset --hard`, `push --force`); **flash vào phần cứng thật**; thay đổi kiến trúc/layer matrix; Worker–Verifier bất đồng > 2 vòng; hai Worker tranh cùng file.
