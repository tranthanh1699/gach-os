# TIÊU CHUẨN COMMIT (CONVENTIONAL COMMITS)

## 1. Cấu trúc

```text
<type>(<scope>): <mô tả, thì hiện tại, chữ thường, không dấu chấm cuối, ≤ 72 ký tự>

[Thân: TẠI SAO thay đổi, chi tiết đáng chú ý]

[Footer: BREAKING CHANGE: ..., Closes #123]
```

## 2. Type

| Type | Mục đích |
| :--- | :--- |
| `feat` | Tính năng mới (app, service, driver, board) |
| `fix` | Sửa lỗi |
| `refactor` | Tái cấu trúc, không đổi hành vi |
| `perf` | Tối ưu hiệu năng / RAM / flash |
| `test` | Test, mock, harness |
| `docs` | Tài liệu, Doxygen |
| `style` | Định dạng (clang-format) |
| `build` | CMake, sdkconfig, idf_component.yml, build.sh |
| `ci` | GitHub Actions |
| `chore` | Việc vặt khác (gitignore, công cụ) |

## 3. Scope (theo tầng / module)

- **Apps**: `(apps)`, `(launcher)`, `(settings)`, `(sysinfo)`, `(stopwatch)`, `(touch_test)`
- **Framework**: `(framework)`, `(app_manager)`, `(shell)`, `(services)`, `(lvgl_port)`, `(theme)`
- **Contracts**: `(hal_api)`, `(mcal)`, `(osal)`, `(common)`
- **Drivers**: `(drivers)`, `(backlight)`
- **Platform**: `(esp32)`, `(esp32/mcal)`, `(jc8012p4a1c)` – tên chip / board
- **Toolchain**: `(build)`, `(docker)`, `(layer_check)`, `(ci)`, `(vscode)`

## 4. Attribution (BẮT BUỘC)

- KHÔNG dùng trailer `Co-Authored-By:` cho AI (Claude, Gemini, Codex, Copilot...).
- KHÔNG thêm "Generated with ...", emoji 🤖 hay chữ ký công cụ AI vào commit message / PR / release note.
- Footer chỉ dành cho `BREAKING CHANGE:`, `Closes #...`, hoặc co-author là **người thật**.

## 5. Ví dụ

```text
feat(settings): add brightness slider backed by fw::Services
fix(jc8012p4a1c): mirror touch y axis to match panel scan
refactor(hal_api): split IBacklight out of IDisplay
build(esp32): pin lvgl to 9.2.2 via component manager
```
