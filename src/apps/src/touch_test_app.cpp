/**
 * @file    touch_test_app.cpp
 * @brief   Hiện thực app kiểm tra cảm ứng.
 */
#include "touch_test_app.hpp"

#include "framework/theme.hpp"
#include "ui_helpers.hpp"

namespace apps {

namespace {

constexpr fw::AppInfo INFO{"touch", "Touch", LV_SYMBOL_EDIT, 0xDB6D28U, true};
constexpr int32_t DOT_SIZE = 8;
constexpr int32_t DOT_HALF = DOT_SIZE / 2;

} // namespace

TouchTestApp::TouchTestApp() : fw::App(INFO), m_p_canvas(nullptr), m_p_coord(nullptr), m_p_dots{}, m_next_dot(0U) {}

void TouchTestApp::on_create(lv_obj_t* p_root)
{
    // Vùng vẽ phủ toàn bộ content; nhận PRESSING liên tục khi kéo ngón tay.
    m_p_canvas = lv_obj_create(p_root);
    lv_obj_remove_style_all(m_p_canvas);
    lv_obj_set_size(m_p_canvas, LV_PCT(100), LV_PCT(100));
    lv_obj_add_flag(m_p_canvas, LV_OBJ_FLAG_CLICKABLE);
    lv_obj_remove_flag(m_p_canvas, LV_OBJ_FLAG_SCROLLABLE);
    lv_obj_add_event_cb(m_p_canvas, &TouchTestApp::on_pressing, LV_EVENT_PRESSING, this);

    for (lv_obj_t*& p_dot : m_p_dots) {
        p_dot = lv_obj_create(m_p_canvas);
        lv_obj_remove_style_all(p_dot);
        lv_obj_set_size(p_dot, DOT_SIZE, DOT_SIZE);
        lv_obj_set_style_radius(p_dot, LV_RADIUS_CIRCLE, 0);
        lv_obj_set_style_bg_opa(p_dot, LV_OPA_COVER, 0);
        lv_obj_set_style_bg_color(p_dot, lv_color_hex(fw::theme::COLOR_SECONDARY), 0);
        lv_obj_remove_flag(p_dot, LV_OBJ_FLAG_CLICKABLE);
        lv_obj_add_flag(p_dot, LV_OBJ_FLAG_HIDDEN);
    }

    m_p_coord = ui::label(p_root, "Touch anywhere", fw::theme::font_small(), fw::theme::COLOR_TEXT_MUTED);
    lv_obj_align(m_p_coord, LV_ALIGN_TOP_LEFT, fw::theme::PAD_NORMAL, fw::theme::PAD_NORMAL);

    lv_obj_t* const p_clear = ui::button(p_root, LV_SYMBOL_TRASH, &TouchTestApp::on_clear_clicked, this);
    lv_obj_align(p_clear, LV_ALIGN_TOP_RIGHT, -fw::theme::PAD_NORMAL, fw::theme::PAD_NORMAL);
}

void TouchTestApp::on_destroy()
{
    // Các object bị framework xóa cùng p_root: chỉ cần bỏ tham chiếu treo.
    m_p_canvas = nullptr;
    m_p_coord = nullptr;
    for (lv_obj_t*& p_dot : m_p_dots) {
        p_dot = nullptr;
    }
    m_next_dot = 0U;
}

void TouchTestApp::clear()
{
    for (lv_obj_t* const p_dot : m_p_dots) {
        lv_obj_add_flag(p_dot, LV_OBJ_FLAG_HIDDEN);
    }
    m_next_dot = 0U;
}

void TouchTestApp::on_pressing(lv_event_t* p_event)
{
    auto* const p_self = static_cast<TouchTestApp*>(lv_event_get_user_data(p_event));
    lv_indev_t* const p_indev = lv_indev_active();
    if (p_indev == nullptr) {
        return;
    }
    lv_point_t point{};
    lv_indev_get_point(p_indev, &point); // Tọa độ màn hình tuyệt đối.

    lv_area_t canvas_area{};
    lv_obj_get_coords(p_self->m_p_canvas, &canvas_area);
    const int32_t local_x = point.x - canvas_area.x1;
    const int32_t local_y = point.y - canvas_area.y1;

    lv_obj_t* const p_dot = p_self->m_p_dots[p_self->m_next_dot];
    lv_obj_set_pos(p_dot, local_x - DOT_HALF, local_y - DOT_HALF);
    lv_obj_remove_flag(p_dot, LV_OBJ_FLAG_HIDDEN);
    p_self->m_next_dot = (p_self->m_next_dot + 1U) % DOT_COUNT;

    lv_label_set_text_fmt(p_self->m_p_coord, "x=%d  y=%d", static_cast<int>(point.x), static_cast<int>(point.y));
}

void TouchTestApp::on_clear_clicked(lv_event_t* p_event)
{
    static_cast<TouchTestApp*>(lv_event_get_user_data(p_event))->clear();
}

} // namespace apps
