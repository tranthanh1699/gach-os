# TIÊU CHUẨN MÃ NGUỒN (EMBEDDED C++17 / ESP-IDF / LVGL)

> Mục tiêu: code đọc như tài liệu, port được sang chip khác chỉ bằng cách viết lại `platform/<chip>`.
> Định dạng tự động: `.clang-format` ở gốc repo (4 spaces, 120 cột).

## 1. Naming

| Đối tượng | Quy ước | Ví dụ |
| :--- | :--- | :--- |
| File | `snake_case` – `.hpp` / `.cpp` | `app_manager.hpp`, `pwm_backlight.cpp` |
| Namespace | `snake_case`, trùng tên tầng | `fw`, `hal`, `mcal`, `osal`, `drivers`, `apps`, `common` |
| Class / Struct | `PascalCase` | `AppManager`, `Xpt2046Config` |
| Interface (thuần ảo) | `I` + `PascalCase` | `IDisplay`, `ISpiDevice` |
| Hàm / method | `snake_case` | `set_brightness()`, `on_create()` |
| Thành viên class | `m_` + `snake_case` (con trỏ: `m_p_`) | `m_stack_depth`, `m_p_display` |
| Biến static file-scope | `s_` + `snake_case` | `s_draw_buf`, `s_launcher` |
| Biến toàn cục (hạn chế tối đa) | `g_` + `snake_case` | `g_fake_tick` |
| Con trỏ (local/param) | `p_` + `snake_case` | `p_root`, `p_event` |
| Hằng `constexpr` / enum value / macro | `UPPER_SNAKE_CASE` | `MAX_APPS`, `Status::ERR_PARAM` |
| Enum | `enum class` + `PascalCase` | `enum class PinLevel : uint8_t` |

## 2. Cấu trúc file

```cpp
/**
 * @file    module.hpp
 * @brief   Một câu mô tả trách nhiệm.
 */
#ifndef LAYER_MODULE_HPP            // Guard = <TẦNG>_<FILE>_HPP
#define LAYER_MODULE_HPP

#include <cstdint>                  // 1. Standard
#include "common/status.hpp"        // 2. Project – LUÔN có tiền tố tầng: "<tầng>/<file>.hpp"
#include "lvgl.h"                   // 3. Thư viện ngoài (chỉ ở tầng được phép)

namespace fw {
...
} // namespace fw

#endif /* LAYER_MODULE_HPP */
```

- Thứ tự include trong `.cpp`: header của chính nó → standard → project → vendor.
- Header public đặt tại `src/<tầng>/include/<tầng>/`. Header private đặt cạnh `.cpp` trong `src/<tầng>/src/`.
- Không `using namespace` trong header. Không include thừa.

## 3. Thiết kế

- **Dependency Injection qua constructor**: driver nhận `mcal::ISpiDevice&`, không tự tạo bus. Composition root duy nhất là `platform/<chip>/boards/<board>/board/src/board.cpp` và `platform/<chip>/main`.
- **Interface hẹp**: mỗi interface 1 trách nhiệm (SRP). Thêm phương thức mới vào interface = thay đổi kiến trúc → cập nhật mọi port.
- **Không exception, không RTTI** (`-fno-exceptions -fno-rtti`): lỗi trả về `common::Status`, giá trị trả qua tham chiếu `out`.
- Class sở hữu tài nguyên phần cứng kế thừa `common::NonCopyable`. Method không đổi trạng thái là `const`. Class lá đánh dấu `final`, method ghi đè dùng `override`.
- Hàm ≤ ~40 dòng, ≤ 4 tham số (nhiều hơn → gom vào `struct XxxConfig`). Early-return thay cho if lồng sâu.
- Không magic number: đặt `constexpr` có tên trong anonymous namespace của `.cpp` hoặc `config.hpp`.

## 4. Bộ nhớ & thời gian thực

1. **Cấm cấp phát động trong `src/`**: không `new`/`delete`/`malloc`/`free`, không `std::vector/string/function/shared_ptr` (bị `tools/layer_check.py` chặn). Đối tượng cấp phát tĩnh; LVGL dùng pool nội bộ (`CONFIG_LV_MEM_SIZE_KILOBYTES`).
2. Kiểu số rõ kích thước `<cstdint>`; `size_t` cho kích thước/chỉ số.
3. Ép kiểu dùng `static_cast` / `reinterpret_cast` (không C-cast). Ngoại lệ duy nhất: `(void)expr;` để bỏ giá trị trả về có chủ đích.
4. Buffer luôn đi kèm độ dài; dùng `snprintf`, cấm `sprintf/strcpy/strcat`.
5. Buffer DMA: RAM nội, căn lề ≥ 4 byte (ESP32), 32 byte nếu chip có D-Cache.
6. Biến chia sẻ ISR ↔ task: `volatile`/atomic + critical section qua OSAL.

## 5. LVGL & luồng

- **Chỉ UI task được gọi API LVGL.** Callback LVGL chạy trong UI task (đã giữ UI lock).
- App chỉ dựng widget dưới `p_root`; object ngoài cây (`lv_timer`, `lv_msgbox` trên top-layer) phải được hủy ở `on_pause`/`on_destroy`.
- Không chặn (blocking) trong callback LVGL > vài ms. Việc nặng → task riêng (qua framework service).

## 6. Doxygen

Mọi API public (class, method, struct, enum) có `@brief`; tham số dùng `@param[in|out|in,out]`; `@return`, `@note`, `@warning` khi cần. Comment inline giải thích **TẠI SAO**, không lặp lại code. `// TODO(owner): lý do`.

## 7. Cờ biên dịch

Code dự án build với `-Wall -Wextra -Werror -Wshadow -Wnon-virtual-dtor` (`OS_STRICT_FLAGS` trong `platform/esp32/CMakeLists.txt`). Không tắt cảnh báo cục bộ nếu không có comment giải thích.
