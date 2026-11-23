/**
 * @file    launcher_app.hpp
 * @brief   [Private] Màn hình chính: lưới icon chia trang, vuốt ngang để chuyển trang.
 *
 * Tiết kiệm RAM LVGL (launcher luôn nằm đáy back-stack):
 *  - Đứng yên chỉ giữ icon trang đang xem; khi vuốt mới dựng thêm trang đích (đỉnh = 2 trang).
 *  - Khi bị app khác che: giải phóng toàn bộ icon, dựng lại khi quay về.
 *  - Style dùng chung cho mọi icon (mỗi icon chỉ giữ riêng màu nền).
 * Lưới co giãn: số cột/hàng mỗi trang tính từ diện tích màn hình (màn nhỏ 4x2, máy tính bảng nhiều hơn).
 */
#ifndef APPS_LAUNCHER_APP_HPP
#define APPS_LAUNCHER_APP_HPP

#include <cstddef>
#include <cstdint>

#include "framework/app.hpp"
#include "framework/config.hpp"

namespace apps {

/** @brief Home screen dạng nhiều trang (kiểu điện thoại). */
class LauncherApp final : public fw::App {
public:
    static constexpr size_t MIN_TILES_PER_PAGE = 8U; /**< Sàn số icon/trang (4x2) -> chặn trên số trang. */
    static constexpr size_t MAX_PAGES = (fw::config::MAX_APPS + MIN_TILES_PER_PAGE - 1U) / MIN_TILES_PER_PAGE;

    LauncherApp();
    void on_create(lv_obj_t* p_root) override;
    void on_resume() override;
    void on_pause() override;

private:
    void build_pages();
    size_t compute_tiles_per_page() const;
    void show_page(size_t page);
    void populate(size_t page);
    void update_indicator();
    void refresh_if_changed();
    void create_tile(lv_obj_t* p_parent, fw::App& target);

    static void on_tile_clicked(lv_event_t* p_event);
    static void on_page_changed(lv_event_t* p_event);
    static void on_scroll_begin(lv_event_t* p_event);
    static void on_timer(lv_timer_t* p_timer);
    static void release_all(void* p_arg);

    lv_obj_t* m_p_tileview;
    lv_obj_t* m_p_indicator;
    lv_obj_t* m_pages[MAX_PAGES];
    fw::App* m_visible[fw::config::MAX_APPS];
    size_t m_visible_count;
    size_t m_tiles_per_page;
    size_t m_page_count;
    size_t m_active_page;
    lv_timer_t* m_p_timer;
    uint32_t m_built_generation;
    bool m_is_paused;
};

} // namespace apps

#endif /* APPS_LAUNCHER_APP_HPP */
