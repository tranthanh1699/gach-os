/**
 * @file    touch_test_app.hpp
 * @brief   [Private] Kiểm tra/hiệu chuẩn cảm ứng: vẽ vệt chạm + hiển thị tọa độ.
 */
#ifndef APPS_TOUCH_TEST_APP_HPP
#define APPS_TOUCH_TEST_APP_HPP

#include <cstddef>

#include "framework/app.hpp"

namespace apps {

/** @brief App kiểm tra cảm ứng. Dùng pool chấm tĩnh (không cấp phát khi chạm). */
class TouchTestApp final : public fw::App {
public:
    static constexpr size_t DOT_COUNT = 48U;

    TouchTestApp();
    void on_create(lv_obj_t* p_root) override;
    void on_destroy() override;

private:
    void clear();
    static void on_pressing(lv_event_t* p_event);
    static void on_clear_clicked(lv_event_t* p_event);

    lv_obj_t* m_p_canvas;
    lv_obj_t* m_p_coord;
    lv_obj_t* m_p_dots[DOT_COUNT];
    size_t m_next_dot;
};

} // namespace apps

#endif /* APPS_TOUCH_TEST_APP_HPP */
