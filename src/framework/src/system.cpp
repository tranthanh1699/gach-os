/**
 * @file    system.cpp
 * @brief   Khởi động OS và vòng lặp UI task (single-threaded LVGL).
 *
 * Toàn bộ thao tác LVGL – kể cả khởi tạo và render khung đầu – chạy trong UI task
 * (stack lớn, chuyên dụng). Task gọi fw::start() chỉ chờ kết quả, nên stack của nó
 * (VD: main task của ESP-IDF) không cần đủ lớn cho việc render.
 */
#include "framework/system.hpp"

#include "common/log.hpp"
#include "common/trace.hpp"
#include "framework/config.hpp"
#include "framework/services.hpp"
#include "framework/theme.hpp"
#include "framework/ui_lock.hpp"
#include "hal_api/board.hpp"
#include "lvgl_port.hpp"
#include "osal/osal.hpp"
#include "shell.hpp"

namespace fw {

namespace {

/** @brief Dữ liệu trao đổi giữa fw::start() và UI task trong pha khởi tạo. */
struct StartContext {
    const hal::Devices* p_devices;
    osal::BinarySemaphore* p_ready;
    common::Status status;
};

Services s_services;
AppManager s_apps{s_services};
LvglPort s_port;
Shell s_shell;
StartContext s_start_ctx{nullptr, nullptr, common::Status::ERR_STATE};
bool s_is_started = false;

/** @brief Khóa LVGL. Khởi tạo lười để không tạo đối tượng RTOS trong static-init. */
osal::Mutex& ui_mutex()
{
    static osal::Mutex s_mutex;
    return s_mutex;
}

/** @brief Cập nhật trạng thái mạng; vừa có kết nối -> đồng bộ giờ từ NTP. */
void on_net_poll(lv_timer_t* p_timer)
{
    (void)p_timer;
    if (s_services.wifi().poll()) {
        s_services.time().on_network_up();
    }
}

/** @brief Khởi tạo thiết bị + LVGL + Shell + app home. Chỉ chạy trong UI task. */
common::Status init_ui(const hal::Devices& devices)
{
    common::Status status = devices.p_display->init();
    if (!common::is_ok(status)) {
        return status;
    }
    // Cảm ứng lỗi không chặn khởi động: hệ thống vẫn hiển thị được.
    hal::ITouch* p_touch = devices.p_touch;
    if ((p_touch != nullptr) && !common::is_ok(p_touch->init())) {
        p_touch = nullptr;
    }
    s_services.bind(devices);

    status = s_port.init(*devices.p_display, p_touch, devices.draw_buffers);
    if (!common::is_ok(status)) {
        return status;
    }

    lv_display_t* const p_disp = s_port.display();
    // Tỷ lệ UI theo màn hình; DPI ảo để widget mặc định của LVGL (lề, bo góc...) co giãn cùng tỷ lệ.
    theme::init(static_cast<int32_t>(devices.p_display->width()), static_cast<int32_t>(devices.p_display->height()));
    lv_display_set_dpi(p_disp, theme::px(LV_DPI_DEF));
    lv_theme_t* const p_theme = lv_theme_default_init(p_disp, lv_color_hex(theme::COLOR_PRIMARY),
                                                      lv_color_hex(theme::COLOR_SECONDARY), true, theme::font_small());
    lv_display_set_theme(p_disp, p_theme);

    s_shell.create(s_apps, s_services);
    status = s_apps.start(s_shell.content(), &s_shell);
    if (!common::is_ok(status)) {
        return status;
    }

    // Vẽ khung đầu tiên trước khi bật đèn nền để không lộ dữ liệu rác của GRAM.
    lv_refr_now(p_disp);
    (void)s_services.set_brightness(config::DEFAULT_BRIGHTNESS);

    // Mạng khởi động sau UI: kết nối lại Wi-Fi đã lưu không làm chậm màn hình đầu tiên.
    s_services.wifi().start();
    (void)lv_timer_create(&on_net_poll, config::NET_POLL_PERIOD_MS, nullptr);
    return common::Status::OK;
}

uint32_t clamp_period(uint32_t wait_ms)
{
    if (wait_ms < config::UI_PERIOD_MIN_MS) {
        return config::UI_PERIOD_MIN_MS;
    }
    if (wait_ms > config::UI_PERIOD_MAX_MS) {
        return config::UI_PERIOD_MAX_MS;
    }
    return wait_ms;
}

volatile uint32_t s_diag_loops = 0U;

void ui_task(void* p_arg)
{
    auto* const p_ctx = static_cast<StartContext*>(p_arg);
    common::Status status = common::Status::OK;
    {
        osal::LockGuard guard(ui_mutex());
        status = init_ui(*p_ctx->p_devices);
    }
    p_ctx->status = status;
    p_ctx->p_ready->give();
    if (!common::is_ok(status)) {
        osal::task_exit();
    }

    for (;;) {
        uint32_t wait_ms = 0U;
        {
            osal::LockGuard guard(ui_mutex());
            diag_set_phase(DIAG_PHASE_HANDLER);
            TRACE_BEGIN("ui", "lv_timer_handler");
            wait_ms = lv_timer_handler();
            TRACE_END("ui", "lv_timer_handler");
            diag_set_phase(DIAG_PHASE_IDLE);
        }
        s_diag_loops = s_diag_loops + 1U;
        osal::delay_ms(clamp_period(wait_ms));
    }
}

} // namespace

void ui_diag(UiDiag& out)
{
    out.loop_count = s_diag_loops;
    out.phase = s_diag_phase;
    out.phase_since_ms = s_diag_phase_ms;
    out.flush_x1 = s_flush_area[0];
    out.flush_y1 = s_flush_area[1];
    out.flush_x2 = s_flush_area[2];
    out.flush_y2 = s_flush_area[3];
    out.flush_count = s_flush_count;
    out.flush_pending = s_flush_pending;
}

AppManager& app_manager()
{
    return s_apps;
}

Services& services()
{
    return s_services;
}

common::Status start(const hal::Devices& devices)
{
    if (s_is_started) {
        return common::Status::ERR_STATE;
    }
    if ((devices.p_display == nullptr) || (devices.p_system == nullptr)) {
        return common::Status::ERR_PARAM;
    }
    s_is_started = true;
    (void)ui_mutex();

    static osal::BinarySemaphore s_ready;
    s_start_ctx = StartContext{&devices, &s_ready, common::Status::ERR_STATE};

    const osal::TaskConfig task_cfg{"ui", config::UI_TASK_STACK_BYTES, config::UI_TASK_PRIORITY, config::UI_TASK_CORE};
    common::Status status = osal::task_create(task_cfg, &ui_task, &s_start_ctx);
    if (common::is_ok(status)) {
        status = s_ready.take(config::UI_START_TIMEOUT_MS) ? s_start_ctx.status : common::Status::ERR_TIMEOUT;
    }
    s_is_started = common::is_ok(status);
    if (s_is_started) {
        LOGI("fw", "UI started: %ux%u, scale %u%%, UI task on core %d", static_cast<unsigned>(devices.p_display->width()),
             static_cast<unsigned>(devices.p_display->height()), static_cast<unsigned>(theme::scale_pct()),
             static_cast<int>(config::UI_TASK_CORE));
    } else {
        LOGE("fw", "UI start failed (status=%u)", static_cast<unsigned>(status));
    }
    return status;
}

UiLock::UiLock()
{
    (void)ui_mutex().lock();
}

UiLock::~UiLock()
{
    ui_mutex().unlock();
}

} // namespace fw
