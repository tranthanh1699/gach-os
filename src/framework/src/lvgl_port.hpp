/**
 * @file    lvgl_port.hpp
 * @brief   [Private] Cầu nối hal::IDisplay / hal::ITouch <-> LVGL v9.
 */
#ifndef FRAMEWORK_LVGL_PORT_HPP
#define FRAMEWORK_LVGL_PORT_HPP

#include "common/non_copyable.hpp"
#include "common/status.hpp"
#include "hal_api/display.hpp"
#include "hal_api/touch.hpp"
#include "lvgl.h"

namespace fw {

/** @brief Gắn display/touch của HAL vào LVGL. Chỉ có 1 instance (tĩnh) trong framework. */
class LvglPort : private common::NonCopyable {
public:
    LvglPort();

    /**
     * @brief Khởi tạo LVGL, tạo lv_display và lv_indev.
     * @param[in] display Màn hình (đã init).
     * @param[in] p_touch Cảm ứng (đã init) hoặc NULL.
     */
    common::Status init(hal::IDisplay& display, hal::ITouch* p_touch);

    /** @return Display LVGL, NULL nếu chưa init. */
    lv_display_t* display() const { return m_p_lv_display; }

private:
    static void flush_cb(lv_display_t* p_disp, const lv_area_t* p_area, uint8_t* p_px_map);
    static void touch_read_cb(lv_indev_t* p_indev, lv_indev_data_t* p_data);
    static uint32_t tick_cb();

    hal::IDisplay* m_p_display;
    hal::ITouch* m_p_touch;
    lv_display_t* m_p_lv_display;
    lv_indev_t* m_p_lv_indev;
    bool m_swap_bytes;
    lv_point_t m_last_point;
};

} // namespace fw

#endif /* FRAMEWORK_LVGL_PORT_HPP */
