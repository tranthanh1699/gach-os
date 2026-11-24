/**
 * @file    bridge_sys.hpp
 * @brief   [Private] Lệnh introspection của debug bridge (sysinfo, ps, top, heap, memmap, health...).
 */
#ifndef RUNTIME_BRIDGE_SYS_HPP
#define RUNTIME_BRIDGE_SYS_HPP

#include <cstddef>
#include <cstdint>

#include "hal_api/board.hpp"

namespace runtime {

/** @brief Việc chạy trên task UI (stack lớn, được gọi LVGL / mở app). */
using UiFn = void (*)(void* p_ctx);

/**
 * @brief Hẹn fn chạy trên task UI rồi chờ xong (tối đa 5 s).
 * @return false nếu UI không xử lý kịp (treo).
 */
bool bridge_run_on_ui(UiFn fn, void* p_ctx);

/** @brief Nhận một dòng log (không có '\n'). */
using LineFn = void (*)(const char* p_line, void* p_ctx);

/** @brief Duyệt từng dòng của lịch sử log (cũ -> mới). */
void bridge_for_each_log_line(LineFn fn, void* p_ctx);

/**
 * @brief Lệnh quản lý app (`app list|info|launch|stop|restart|kill|install|uninstall|enable|disable|status|logs|
 *        permissions|inspect`).
 * @return false nếu không phải lệnh `app`.
 */
bool bridge_app_command(uint8_t id, size_t argc, char* const p_args[], const hal::Devices& devices);

/** @brief Gửi một dòng kết quả (khung TEXT) của lệnh id. */
void bridge_text(uint8_t id, const char* p_text);

/** @brief Kết thúc lệnh id (khung RESP). */
void bridge_resp(uint8_t id, bool is_ok, const char* p_text);

/**
 * @brief Xử lý lệnh introspection.
 * @return false nếu không phải lệnh của nhóm này (bridge xử lý tiếp / báo unknown).
 */
bool bridge_sys_command(uint8_t id, size_t argc, char* const p_args[], const hal::Devices& devices);


} // namespace runtime

#endif /* RUNTIME_BRIDGE_SYS_HPP */
