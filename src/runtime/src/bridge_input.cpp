/**
 * @file    bridge_input.cpp
 * @brief   Lệnh `input` của debug bridge (kiểu `adb shell input`): chạm, giữ, vuốt, giữ rồi kéo.
 *
 * Điểm chạm giả đi qua fw::input_inject(): LVGL đọc như cảm ứng thật (cùng đường xử lý nhấn / giữ / cuộn).
 * Tọa độ logic (pixel màn hình). Bước di chuyển mỗi 16 ms (một lần quét touch).
 */
#include <cstdio>
#include <cstdlib>
#include <cstring>

#include "bridge_sys.hpp"
#include "framework/system.hpp"
#include "osal/osal.hpp"

namespace runtime {

namespace {

constexpr uint32_t STEP_MS = 16U;
constexpr uint32_t TAP_MS = 80U;
constexpr uint32_t PRESS_DEFAULT_MS = 800U;   /**< Giữ lâu (long press của LVGL ~400 ms). */
constexpr uint32_t SWIPE_DEFAULT_MS = 300U;
constexpr uint32_t DRAG_HOLD_DEFAULT_MS = 800U;
constexpr uint32_t DRAG_MOVE_DEFAULT_MS = 600U;
constexpr uint32_t RELEASE_MS = 50U;
constexpr uint32_t MAX_MS = 10000U;

int32_t num(const char* p_text)
{
    return (p_text != nullptr) ? static_cast<int32_t>(std::strtol(p_text, nullptr, 10)) : 0;
}

uint32_t duration(const char* p_text, uint32_t fallback)
{
    const uint32_t ms = (p_text != nullptr) ? static_cast<uint32_t>(std::strtoul(p_text, nullptr, 10)) : fallback;
    return (ms > MAX_MS) ? MAX_MS : ms;
}

void hold(int32_t x, int32_t y, uint32_t ms)
{
    fw::input_inject(true, x, y, true);
    osal::delay_ms(ms);
}

void move(int32_t x1, int32_t y1, int32_t x2, int32_t y2, uint32_t ms)
{
    const uint32_t steps = (ms / STEP_MS) + 1U;
    for (uint32_t step = 1U; step <= steps; ++step) {
        const auto x = static_cast<int32_t>(x1 + ((x2 - x1) * static_cast<int32_t>(step)) / static_cast<int32_t>(steps));
        const auto y = static_cast<int32_t>(y1 + ((y2 - y1) * static_cast<int32_t>(step)) / static_cast<int32_t>(steps));
        hold(x, y, STEP_MS);
    }
}

void release(int32_t x, int32_t y)
{
    fw::input_inject(true, x, y, false);
    osal::delay_ms(RELEASE_MS);
    fw::input_inject(false, 0, 0, false);
}

} // namespace

bool bridge_input_command(uint8_t id, size_t argc, char* const p_args[])
{
    if ((argc == 0U) || (std::strcmp(p_args[0], "input") != 0)) {
        return false;
    }
    const char* const p_sub = (argc > 1U) ? p_args[1] : "";
    const auto arg = [argc, p_args](size_t index) { return (index < argc) ? p_args[index] : nullptr; };
    if ((std::strcmp(p_sub, "tap") == 0) && (argc >= 4U)) {
        hold(num(arg(2)), num(arg(3)), TAP_MS);
        release(num(arg(2)), num(arg(3)));
    } else if ((std::strcmp(p_sub, "press") == 0) && (argc >= 4U)) {
        hold(num(arg(2)), num(arg(3)), duration(arg(4), PRESS_DEFAULT_MS));
        release(num(arg(2)), num(arg(3)));
    } else if ((std::strcmp(p_sub, "swipe") == 0) && (argc >= 6U)) {
        hold(num(arg(2)), num(arg(3)), STEP_MS);
        move(num(arg(2)), num(arg(3)), num(arg(4)), num(arg(5)), duration(arg(6), SWIPE_DEFAULT_MS));
        release(num(arg(4)), num(arg(5)));
    } else if ((std::strcmp(p_sub, "drag") == 0) && (argc >= 6U)) {
        hold(num(arg(2)), num(arg(3)), duration(arg(6), DRAG_HOLD_DEFAULT_MS));
        move(num(arg(2)), num(arg(3)), num(arg(4)), num(arg(5)), duration(arg(7), DRAG_MOVE_DEFAULT_MS));
        hold(num(arg(4)), num(arg(5)), RELEASE_MS); // Dừng tại đích trước khi thả (thả không bị hiểu là vuốt).
        release(num(arg(4)), num(arg(5)));
    } else {
        bridge_resp(id, false, "usage: input tap x y | press x y [ms] | swipe x1 y1 x2 y2 [ms] | drag x1 y1 x2 y2 "
                               "[hold_ms] [ms]");
        return true;
    }
    bridge_resp(id, true, nullptr);
    return true;
}

} // namespace runtime
