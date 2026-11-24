# Chẩn đoán: trace, sự kiện, sổ lỗi, core dump, watchdog

Bộ công cụ để tìm nguyên nhân khi thiết bị chậm, lỗi hoặc tự khởi động lại. Mọi lệnh chạy qua `tools/gach`
(một dòng) hoặc trong `gach shell`. `help diag` / `help logs` liệt kê cú pháp.

## 1. Dùng nhanh

```bash
tools/gach diagnose                       # một báo cáo: health + crash + sổ lỗi + lỗi trong log + sự kiện + gợi ý
tools/gach log -l w 30                    # 30 dòng log mức W/E gần nhất
tools/gach log filter wifi -l w           # dòng log chứa "wifi", mức W hoặc nặng hơn
tools/gach log export boot.log            # lưu lịch sử log (32 KiB) ra file
tools/gach event monitor                  # theo dõi sự kiện hệ thống trực tiếp (Ctrl-C)
tools/gach trace start; <thao tác>; tools/gach trace export ui.json   # mở ui.json trong ui.perfetto.dev
tools/gach fault list                     # lỗi đã xảy ra (giữ qua khởi động lại)
tools/gach crash info                     # lần crash gần nhất: task, pc, nguyên nhân
tools/gach crash dump                     # lấy core dump về + giải backtrace (cần đúng file .elf đã nạp)
```

## 2. Lệnh

| Lệnh | Tác dụng |
| :--- | :--- |
| `log [n] [-l e\|w\|i\|d\|v]` | n dòng cuối của lịch sử log (mặc định 50); `-l w` = chỉ mức W và nặng hơn |
| `log filter <tag\|chữ> [-l w] [-n N]` | Dòng log chứa chuỗi (so nguyên văn, phân biệt hoa thường) |
| `log level [tag] [lvl]` | Như `loglevel` |
| `log tail` / `log export <file>` | [pc] Chỉ log mới (như `logcat -f`) / lưu lịch sử ra file |
| `event list [n]` / `event since <seq>` | Sự kiện hệ thống (mặc định 30 gần nhất). RESP = `seq N` (sự kiện mới nhất) |
| `event monitor` | [pc] Hỏi `event since` mỗi giây, in sự kiện mới |
| `trace start\|stop\|status` | Ghi trace vào bộ đệm 8192 bản ghi (PSRAM); tự dừng nhận khi đầy (đếm `dropped`) |
| `trace dump` | Bản ghi dạng nén (dùng bởi `trace export`); phải `trace stop` trước |
| `trace export [file.json]` | [pc] Dừng, tải, đổi sang Trace Event Format → chrome://tracing hoặc ui.perfetto.dev |
| `fault list` / `fault clear` | Sổ lỗi bền (NVS, 16 mục gần nhất): `boot#`, giây kể từ boot, nguồn, nội dung |
| `crash info` / `crash clear` | Tóm tắt core dump trong flash (task, pc, ra, mcause, mtval, SHA firmware) / xóa |
| `crash save` | Board chép core dump ra thẻ: `/crash/coredump.bin` |
| `crash dump [file]` | [pc] `crash save` + `pull` + chạy `esp-coredump` (trên máy hoặc trong Docker của `build.sh`) với `.elf` có SHA khớp |
| `crash test --yes` | Cố ý gây panic để kiểm tra đường core dump (board khởi động lại) |
| `watchdog info` | Task WDT (timeout, panic hay chỉ cảnh báo, core nào được theo dõi), interrupt WDT |
| `diagnose` | `health` + crash + sổ lỗi + đếm E/W trong log + 8 sự kiện gần nhất + gợi ý bước tiếp |

## 3. Nguồn dữ liệu

| Nguồn | Ai ghi | Lưu ở | Mất khi |
| :--- | :--- | :--- | :--- |
| Lịch sử log | mọi `LOGx` / `ESP_LOGx` | RAM (32 KiB) | khởi động lại |
| Sự kiện | `EVENT_I/W/E` (`common/event.hpp`) | RAM, 256 mục | khởi động lại |
| Trace | `TRACE_*` (`common/trace.hpp`) | PSRAM, 8192 bản ghi | `trace start` / khởi động lại |
| Sổ lỗi | `runtime::fault_record` | NVS key `faults` | `fault clear` |
| Core dump | panic handler của ESP-IDF | phân vùng flash `coredump` (128 KiB) | `crash clear` / panic kế tiếp |

Sự kiện đang ghi: mở/đóng app (`app`), Wi-Fi kết nối/mất/lỗi (`wifi`), cài/gỡ app (`runtime`), lỗi script Lua
(`script`), reset bất thường + có core dump lúc boot (`system`). Mỗi sự kiện cũng là một dòng log cùng mức.

Sổ lỗi nhận: reset bất thường (panic, watchdog, brownout... – kèm task/pc/nguyên nhân nếu do crash), lỗi runtime
(cài gói, quét app), lỗi script.

Trace đang ghi: `ui/lv_timer_handler` (mỗi vòng UI), `ui/render` (một khung), `ui/flush` (giá trị = số pixel),
`input/touch down|up` (giá trị = x), `app/open` (thời gian `on_create`), `game/tick` (một bước thế giới sprite của
`gach.game`), `game/lua_update` (một lần `on_update` của app), `game/draw` (dựng lệnh vẽ scene cho một dải).

`uidiag` (không cần khóa UI) còn in vùng flush gần nhất và `PENDING` nếu một lần flush bất đồng bộ chưa báo xong:
UI đứng mà thấy cùng một flush PENDING qua nhiều lần gọi là màn hình / PPA không trả lời.

## 4. Dùng trong code

```cpp
#include "common/event.hpp"
#include "common/trace.hpp"

EVENT_W("wifi", "connect to '%s' failed", ssid);   // sự kiện + dòng log W
void Foo::step() {
    TRACE_SCOPE("foo", "step");                     // BEGIN ở đây, END khi ra khỏi scope
    TRACE_INSTANT("foo", "tick", count);            // một mốc kèm giá trị
    TRACE_COUNTER("foo", "queue", depth);           // đồ thị giá trị theo thời gian
}
```

- `cat` / `name` phải là chuỗi tĩnh (chỉ lưu con trỏ). Khi trace tắt, mỗi macro chỉ tốn một phép đọc cờ.
- Cả hai header thuộc tầng L0 (`common`), dùng được ở mọi tầng; backend nằm ở `src/runtime/src/diag.cpp`,
  cắm bằng `runtime::diag_install()` đầu `app_main`.
- Mỗi bản ghi trace lưu tên task RTOS (mỗi task là một hàng trong Perfetto). Tên của task đã bị xóa trước lúc dump có
  thể hiện sai.

## 5. Core dump

- Cấu hình (board `jc8012p4a1c`): `CONFIG_ESP_COREDUMP_ENABLE_TO_FLASH`, định dạng ELF, checksum SHA256; phân vùng
  `coredump` 128 KiB ở cuối bảng phân vùng (dump thực tế ~14 KiB; dư cho nhiều task / stack lớn hơn).
- Giải mã cần **đúng file `.elf` đã chạy lúc crash** (so SHA256). `crash dump` tự tìm trong `build/*/gach_os.elf`;
  build lại từ mã nguồn khác sẽ không khớp.
- Không cần `.elf`: `crash info` cho `pc` / `ra` → `riscv32-esp-elf-addr2line -pfiaC -e <elf> <pc> <ra>`.
- Task WDT hiện chỉ cảnh báo (không panic): task bị treo không làm khởi động lại, xem log `task_wdt`.
