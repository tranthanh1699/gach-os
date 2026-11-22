/**
 * @file    lvgl_port.cpp
 * @brief   Hiện thực cầu nối HAL <-> LVGL.
 */
#include "lvgl_port.hpp"

#include "framework/config.hpp"
#include "osal/osal.hpp"

namespace fw {

namespace {

// Buffer render tĩnh trong RAM nội (DMA-capable trên ESP32), căn lề theo yêu cầu DMA/LVGL.
alignas(4) uint8_t s_draw_buf[config::DRAW_BUF_BYTES];

} // namespace

LvglPort::LvglPort()
    : m_p_display(nullptr), m_p_touch(nullptr), m_p_lv_display(nullptr), m_p_lv_indev(nullptr), m_swap_bytes(false),
      m_last_point{0, 0}
{
}

common::Status LvglPort::init(hal::IDisplay& display, hal::ITouch* p_touch)
{
    if (m_p_lv_display != nullptr) {
        return common::Status::ERR_STATE;
    }
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
    lv_display_set_buffers(m_p_lv_display, s_draw_buf, nullptr, sizeof(s_draw_buf), LV_DISPLAY_RENDER_MODE_PARTIAL);
    lv_display_set_flush_cb(m_p_lv_display, &LvglPort::flush_cb);
    lv_display_set_user_data(m_p_lv_display, this);

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
    // Lỗi truyền 1 khung không được làm treo UI: vẫn báo flush_ready để LVGL tiếp tục.
    (void)p_self->m_p_display->draw_bitmap(area, reinterpret_cast<const uint16_t*>(p_px_map));
    lv_display_flush_ready(p_disp);
}

void LvglPort::touch_read_cb(lv_indev_t* p_indev, lv_indev_data_t* p_data)
{
    auto* const p_self = static_cast<LvglPort*>(lv_indev_get_user_data(p_indev));
    hal::TouchPoint point{};
    const bool is_pressed = common::is_ok(p_self->m_p_touch->read(point)) && point.pressed;
    if (is_pressed) {
        p_self->m_last_point.x = static_cast<int32_t>(point.x);
        p_self->m_last_point.y = static_cast<int32_t>(point.y);
    }
    p_data->point = p_self->m_last_point;
    p_data->state = is_pressed ? LV_INDEV_STATE_PRESSED : LV_INDEV_STATE_RELEASED;
}

uint32_t LvglPort::tick_cb()
{
    return osal::tick_ms();
}

} // namespace fw
