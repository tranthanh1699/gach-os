/**
 * @file    lvgl_port.cpp
 * @brief   Hiện thực cầu nối HAL <-> LVGL.
 */
#include "lvgl_port.hpp"

#include "framework/config.hpp"
#include "framework/system.hpp"
#include "common/trace.hpp"
#include "osal/osal.hpp"

namespace fw {

namespace {

// Số liệu hiệu năng (chỉ task UI ghi; đọc qua perf_snapshot() khi giữ UiLock).
PerfStats s_perf{};
uint32_t s_perf_window_start_ms = 0U;
uint32_t s_render_start_us = 0U;

} // namespace

volatile uint32_t s_diag_phase = DIAG_PHASE_IDLE;
uint32_t s_band_lines = 1U; /**< Số dòng một dải vẽ (buffer / chiều rộng): mốc căn vùng vẽ lại khi flush async. */
volatile int32_t s_flush_area[4] = {0, 0, 0, 0};
volatile uint32_t s_flush_count = 0U;
volatile bool s_flush_pending = false;
volatile uint32_t s_diag_phase_ms = 0U;

void diag_set_phase(uint32_t phase)
{
    s_diag_phase = phase;
    s_diag_phase_ms = osal::tick_ms();
}

LvglPort::LvglPort()
    : m_p_display(nullptr), m_p_touch(nullptr), m_p_lv_display(nullptr), m_p_lv_indev(nullptr), m_swap_bytes(false),
      m_is_async(false),
      m_last_point{0, 0}
{
}

common::Status LvglPort::init(hal::IDisplay& display, hal::ITouch* p_touch, const hal::MemoryRegion* p_buffers)
{
    if (m_p_lv_display != nullptr) {
        return common::Status::ERR_STATE;
    }
    if ((p_buffers == nullptr) || (p_buffers[0].p_base == nullptr) || (p_buffers[0].size == 0U)) {
        return common::Status::ERR_PARAM; // Board phải cấp buffer vẽ (hal::Devices::draw_buffers).
    }
    const hal::MemoryRegion& second = p_buffers[1];
    // Buffer thứ hai chỉ có ích khi màn hình đẩy pixel bất đồng bộ (vẽ buffer này trong lúc đẩy buffer kia).
    m_is_async = display.supports_async_flush();
    const bool is_double = m_is_async && (second.p_base != nullptr) && (second.size == p_buffers[0].size);
    m_p_display = &display;
    m_p_touch = p_touch;
    m_swap_bytes = (display.pixel_format() == hal::PixelFormat::RGB565_SWAPPED);

    lv_init();
    lv_tick_set_cb(&LvglPort::tick_cb);

    m_p_lv_display = lv_display_create(static_cast<int32_t>(display.width()), static_cast<int32_t>(display.height()));
    if (m_p_lv_display == nullptr) {
        return common::Status::ERR_NO_MEM;
    }
    lv_display_set_color_format(m_p_lv_display, LV_COLOR_FORMAT_RGB565);
    lv_display_set_buffers(m_p_lv_display, p_buffers[0].p_base, is_double ? second.p_base : nullptr,
                           static_cast<uint32_t>(p_buffers[0].size), LV_DISPLAY_RENDER_MODE_PARTIAL);
    lv_display_set_flush_cb(m_p_lv_display, &LvglPort::flush_cb);
    lv_display_set_user_data(m_p_lv_display, this);
    lv_display_add_event_cb(m_p_lv_display, &LvglPort::render_event_cb, LV_EVENT_RENDER_START, nullptr);
    lv_display_add_event_cb(m_p_lv_display, &LvglPort::render_event_cb, LV_EVENT_RENDER_READY, nullptr);
    s_band_lines = static_cast<uint32_t>(p_buffers[0].size) / (display.width() * sizeof(uint16_t));
    s_band_lines = (s_band_lines == 0U) ? 1U : s_band_lines;
    if (m_is_async) {
        // Flush bất đồng bộ (ESP32-P4: PPA xoay vào framebuffer): khối hẹp mà cao (VD 288x71) có thể làm PPA treo
        // vĩnh viễn (lỗi phần cứng DIG-734, workaround của IDF chưa che hết) -> LVGL chờ flush mãi, UI đứng. Dải
        // full chiều rộng nhưng mỏng (VD 1280x6, gặp khi kết thúc màn game) cũng treo.
        // Mọi vùng vẽ lại được căn ra full chiều rộng và theo bội số dòng của buffer: mọi khối flush đều đúng
        // kích thước dải chuẩn (1280x16 trên JC8012P4A1C), dạng đã chạy ổn qua mọi phép đo hiệu năng.
        lv_display_add_event_cb(m_p_lv_display, &LvglPort::invalidate_event_cb, LV_EVENT_INVALIDATE_AREA, nullptr);
    }
    s_perf_window_start_ms = osal::tick_ms();

    if (m_p_touch != nullptr) {
        m_p_lv_indev = lv_indev_create();
        if (m_p_lv_indev == nullptr) {
            return common::Status::ERR_NO_MEM;
        }
        lv_indev_set_type(m_p_lv_indev, LV_INDEV_TYPE_POINTER);
        lv_indev_set_read_cb(m_p_lv_indev, &LvglPort::touch_read_cb);
        lv_indev_set_user_data(m_p_lv_indev, this);
        lv_indev_set_display(m_p_lv_indev, m_p_lv_display);
    }
    return common::Status::OK;
}

void LvglPort::flush_cb(lv_display_t* p_disp, const lv_area_t* p_area, uint8_t* p_px_map)
{
    auto* const p_self = static_cast<LvglPort*>(lv_display_get_user_data(p_disp));
    if (p_self->m_swap_bytes) {
        lv_draw_sw_rgb565_swap(p_px_map, lv_area_get_size(p_area));
    }
    const hal::Area area{p_area->x1, p_area->y1, p_area->x2, p_area->y2};
    diag_set_phase(DIAG_PHASE_FLUSH);
    TRACE_INSTANT("ui", "flush", static_cast<int32_t>(lv_area_get_size(p_area)));
    const uint32_t start_us = osal::time_us();
    s_flush_area[0] = area.x1;
    s_flush_area[1] = area.y1;
    s_flush_area[2] = area.x2;
    s_flush_area[3] = area.y2;
    s_flush_count = s_flush_count + 1U;
    if (p_self->m_is_async) {
        s_flush_pending = true;
        // Trả về ngay: LVGL vẽ dải kế tiếp vào buffer kia; on_flush_done (ISR) báo flush_ready.
        const common::Status status = p_self->m_p_display->draw_bitmap_async(
            area, reinterpret_cast<const uint16_t*>(p_px_map), &LvglPort::on_flush_done, p_disp);
        s_perf.flush_us += osal::time_us() - start_us;
        ++s_perf.flush_count;
        s_perf.flushed_px += static_cast<uint32_t>(lv_area_get_size(p_area));
        if (!common::is_ok(status)) {
            s_flush_pending = false;
            lv_display_flush_ready(p_disp); // Lỗi khởi động: không để LVGL chờ mãi.
        }
        return;
    }
    // Lỗi truyền 1 khung không được làm treo UI: vẫn báo flush_ready để LVGL tiếp tục.
    (void)p_self->m_p_display->draw_bitmap(area, reinterpret_cast<const uint16_t*>(p_px_map));
    s_perf.flush_us += osal::time_us() - start_us;
    ++s_perf.flush_count;
    s_perf.flushed_px += static_cast<uint32_t>(lv_area_get_size(p_area));
    lv_display_flush_ready(p_disp);
}

void LvglPort::invalidate_event_cb(lv_event_t* p_event)
{
    auto* const p_area = static_cast<lv_area_t*>(lv_event_get_param(p_event));
    lv_display_t* const p_disp = static_cast<lv_display_t*>(lv_event_get_target(p_event));
    const int32_t band = static_cast<int32_t>(s_band_lines);
    const int32_t last_row = lv_display_get_vertical_resolution(p_disp) - 1;
    p_area->x1 = 0;
    p_area->x2 = lv_display_get_horizontal_resolution(p_disp) - 1;
    // Mép trên / dưới theo bội số dòng của buffer: LVGL chia vùng thành dải `band` dòng tính từ y1, nên mọi lần
    // flush đều là khối đủ chiều rộng x `band` dòng (không còn dải mỏng 6 dòng làm PPA treo).
    p_area->y1 = (p_area->y1 / band) * band;
    const int32_t y2 = ((p_area->y2 / band) + 1) * band - 1;
    p_area->y2 = (y2 > last_row) ? last_row : y2;
}

void LvglPort::on_flush_done(void* p_ctx)
{
    s_flush_pending = false;
    lv_display_flush_ready(static_cast<lv_display_t*>(p_ctx)); // Chỉ đặt cờ: an toàn khi gọi từ ISR.
}

void LvglPort::touch_read_cb(lv_indev_t* p_indev, lv_indev_data_t* p_data)
{
    auto* const p_self = static_cast<LvglPort*>(lv_indev_get_user_data(p_indev));
    hal::TouchPoint point{};
    diag_set_phase(DIAG_PHASE_TOUCH);
    const bool is_pressed = common::is_ok(p_self->m_p_touch->read(point)) && point.pressed;
    static bool s_was_pressed = false; // Một indev cảm ứng: chỉ ghi trace lúc đổi trạng thái.
    if (is_pressed != s_was_pressed) {
        s_was_pressed = is_pressed;
        TRACE_INSTANT("input", is_pressed ? "touch down" : "touch up", static_cast<int32_t>(point.x));
    }
    if (is_pressed) {
        p_self->m_last_point.x = static_cast<int32_t>(point.x);
        p_self->m_last_point.y = static_cast<int32_t>(point.y);
    }
    p_data->point = p_self->m_last_point;
    p_data->state = is_pressed ? LV_INDEV_STATE_PRESSED : LV_INDEV_STATE_RELEASED;
}

void LvglPort::render_event_cb(lv_event_t* p_event)
{
    if (lv_event_get_code(p_event) == LV_EVENT_RENDER_START) {
        diag_set_phase(DIAG_PHASE_RENDER);
        TRACE_BEGIN("ui", "render");
        s_render_start_us = osal::time_us();
        return;
    }
    TRACE_END("ui", "render");
    const uint32_t frame_us = osal::time_us() - s_render_start_us;
    s_perf.render_us += frame_us;
    s_perf.max_frame_us = (frame_us > s_perf.max_frame_us) ? frame_us : s_perf.max_frame_us;
    ++s_perf.frames;
}

void perf_snapshot(PerfStats& out, bool reset)
{
    out = s_perf;
    out.window_ms = osal::tick_ms() - s_perf_window_start_ms;
    if (reset) {
        s_perf = PerfStats{};
        s_perf_window_start_ms = osal::tick_ms();
    }
}

uint32_t s_bench_rounds = 0U;
bool s_bench_done = false;
PerfStats s_bench_result{};

/** @brief Chạy trong task UI (lv_timer): vẽ lại toàn màn N lần, lưu số liệu riêng của các lần đó. */
void bench_timer_cb(lv_timer_t* p_timer)
{
    (void)p_timer;
    const PerfStats saved = s_perf;
    const uint32_t saved_start = s_perf_window_start_ms;
    s_perf = PerfStats{};
    s_perf_window_start_ms = osal::tick_ms();
    lv_display_t* const p_disp = lv_display_get_default();
    for (uint32_t round = 0U; (p_disp != nullptr) && (round < s_bench_rounds); ++round) {
        lv_obj_invalidate(lv_screen_active());
        lv_refr_now(p_disp);
    }
    perf_snapshot(s_bench_result, false);
    s_perf = saved; // Không làm lệch cửa sổ đo đang chạy của `perf`.
    s_perf_window_start_ms = saved_start;
    s_bench_done = true;
}

void perf_bench_start(uint32_t rounds)
{
    s_bench_rounds = rounds;
    s_bench_done = false;
    lv_timer_t* const p_timer = lv_timer_create(&bench_timer_cb, 0U, nullptr);
    lv_timer_set_repeat_count(p_timer, 1); // Một lần rồi tự xóa.
}

bool perf_bench_result(PerfStats& out)
{
    if (!s_bench_done) {
        return false;
    }
    out = s_bench_result;
    return true;
}

uint32_t LvglPort::tick_cb()
{
    return osal::tick_ms();
}

} // namespace fw
