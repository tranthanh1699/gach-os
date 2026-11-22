/**
 * @file    stopwatch_app.hpp
 * @brief   [Private] Đồng hồ bấm giờ. Trạng thái giữ nguyên khi app chạy nền trong back-stack.
 */
#ifndef APPS_STOPWATCH_APP_HPP
#define APPS_STOPWATCH_APP_HPP

#include <cstdint>

#include "framework/app.hpp"

namespace apps {

/** @brief App bấm giờ (độ phân giải 0.1 s). */
class StopwatchApp final : public fw::App {
public:
    StopwatchApp();
    void on_create(lv_obj_t* p_root) override;
    void on_resume() override;
    void on_pause() override;

private:
    uint32_t elapsed_ms() const;
    void refresh();
    static void on_timer(lv_timer_t* p_timer);
    static void on_start_stop(lv_event_t* p_event);
    static void on_reset(lv_event_t* p_event);

    lv_obj_t* m_p_time;
    lv_obj_t* m_p_start_label;
    lv_timer_t* m_p_timer;
    bool m_is_running;
    uint32_t m_start_ms;
    uint32_t m_accumulated_ms;
};

} // namespace apps

#endif /* APPS_STOPWATCH_APP_HPP */
