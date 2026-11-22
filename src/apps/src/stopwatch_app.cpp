/**
 * @file    stopwatch_app.cpp
 * @brief   Hiện thực app bấm giờ.
 */
#include "stopwatch_app.hpp"

#include "framework/services.hpp"
#include "framework/theme.hpp"
#include "ui_helpers.hpp"

namespace apps {

namespace {

constexpr fw::AppInfo INFO{"stopwatch", "Stopwatch", LV_SYMBOL_BELL, 0x3FB950U, true};
constexpr uint32_t REFRESH_PERIOD_MS = 100U;
constexpr uint32_t MS_PER_TENTH = 100U;
constexpr uint32_t TENTHS_PER_SECOND = 10U;
constexpr uint32_t SECONDS_PER_MINUTE = 60U;
constexpr uint32_t MINUTES_WRAP = 100U;
constexpr int32_t BUTTON_WIDTH = 110;

} // namespace

StopwatchApp::StopwatchApp()
    : fw::App(INFO), m_p_time(nullptr), m_p_start_label(nullptr), m_p_timer(nullptr), m_is_running(false),
      m_start_ms(0U), m_accumulated_ms(0U)
{
}

void StopwatchApp::on_create(lv_obj_t* p_root)
{
    ui::column_layout(p_root);
    lv_obj_set_flex_align(p_root, LV_FLEX_ALIGN_CENTER, LV_FLEX_ALIGN_CENTER, LV_FLEX_ALIGN_CENTER);

    m_p_time = ui::label(p_root, "", fw::theme::font_large(), fw::theme::COLOR_TEXT);

    lv_obj_t* const p_row = lv_obj_create(p_root);
    lv_obj_remove_style_all(p_row);
    lv_obj_set_size(p_row, LV_PCT(100), LV_SIZE_CONTENT);
    lv_obj_set_flex_flow(p_row, LV_FLEX_FLOW_ROW);
    lv_obj_set_flex_align(p_row, LV_FLEX_ALIGN_SPACE_EVENLY, LV_FLEX_ALIGN_CENTER, LV_FLEX_ALIGN_CENTER);

    lv_obj_t* const p_start = ui::button(p_row, "", &StopwatchApp::on_start_stop, this);
    lv_obj_set_width(p_start, BUTTON_WIDTH);
    m_p_start_label = lv_obj_get_child(p_start, 0);
    lv_obj_t* const p_reset = ui::button(p_row, LV_SYMBOL_REFRESH " Reset", &StopwatchApp::on_reset, this);
    lv_obj_set_width(p_reset, BUTTON_WIDTH);
}

void StopwatchApp::on_resume()
{
    refresh();
    m_p_timer = lv_timer_create(&StopwatchApp::on_timer, REFRESH_PERIOD_MS, this);
}

void StopwatchApp::on_pause()
{
    // Chỉ dừng cập nhật UI; thời gian vẫn tính theo uptime nên không bị lệch khi quay lại.
    if (m_p_timer != nullptr) {
        lv_timer_delete(m_p_timer);
        m_p_timer = nullptr;
    }
}

uint32_t StopwatchApp::elapsed_ms() const
{
    if (!m_is_running) {
        return m_accumulated_ms;
    }
    return m_accumulated_ms + (context().services.uptime_ms() - m_start_ms);
}

void StopwatchApp::refresh()
{
    const uint32_t tenths = elapsed_ms() / MS_PER_TENTH;
    const uint32_t seconds = tenths / TENTHS_PER_SECOND;
    lv_label_set_text_fmt(m_p_time, "%02u:%02u.%u", static_cast<unsigned>((seconds / SECONDS_PER_MINUTE) % MINUTES_WRAP),
                          static_cast<unsigned>(seconds % SECONDS_PER_MINUTE),
                          static_cast<unsigned>(tenths % TENTHS_PER_SECOND));
    lv_label_set_text(m_p_start_label, m_is_running ? LV_SYMBOL_PAUSE " Stop" : LV_SYMBOL_PLAY " Start");
}

void StopwatchApp::on_timer(lv_timer_t* p_timer)
{
    static_cast<StopwatchApp*>(lv_timer_get_user_data(p_timer))->refresh();
}

void StopwatchApp::on_start_stop(lv_event_t* p_event)
{
    auto* const p_self = static_cast<StopwatchApp*>(lv_event_get_user_data(p_event));
    const uint32_t now = p_self->context().services.uptime_ms();
    if (p_self->m_is_running) {
        p_self->m_accumulated_ms += now - p_self->m_start_ms;
    } else {
        p_self->m_start_ms = now;
    }
    p_self->m_is_running = !p_self->m_is_running;
    p_self->refresh();
}

void StopwatchApp::on_reset(lv_event_t* p_event)
{
    auto* const p_self = static_cast<StopwatchApp*>(lv_event_get_user_data(p_event));
    p_self->m_accumulated_ms = 0U;
    p_self->m_start_ms = p_self->context().services.uptime_ms();
    p_self->refresh();
}

} // namespace apps
