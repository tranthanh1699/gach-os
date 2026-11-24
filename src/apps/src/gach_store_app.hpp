/**
 * @file    gach_store_app.hpp
 * @brief   [Private] Gach Store: kho ứng dụng trực tuyến (danh mục trên GitHub Pages), cài / cập nhật app qua Wi-Fi,
 *          trang chi tiết có mô tả và ảnh chụp.
 *
 * Cài từ thẻ SD: app Files. Gỡ app: Settings -> Apps, hoặc giữ icon trên màn hình chính.
 */
#ifndef APPS_GACH_STORE_APP_HPP
#define APPS_GACH_STORE_APP_HPP

#include <cstddef>
#include <cstdint>

#include "framework/app.hpp"
#include "runtime/runtime.hpp"

namespace apps {

/** @brief App "Gach Store". */
class GachStoreApp final : public fw::App {
public:
    static constexpr size_t MAX_SHOTS = 3U;

    GachStoreApp();
    void on_create(lv_obj_t* p_root) override;
    void on_resume() override;
    void on_pause() override;
    bool on_back() override;

private:
    enum class Page : uint8_t { DISCOVER, UPDATES, DETAIL };
    enum class Action : uint8_t { GET, UPDATE, OPEN, UNAVAILABLE };

    void render();
    void render_discover();
    void render_updates();
    void render_detail();
    void render_message(const char* p_symbol, const char* p_text, bool show_retry);
    lv_obj_t* add_section_title(lv_obj_t* p_parent, const char* p_text);
    lv_obj_t* add_app_card(lv_obj_t* p_parent, size_t index);
    lv_obj_t* add_icon(lv_obj_t* p_parent, const runtime::StoreApp& app, int32_t size_dp);
    lv_obj_t* add_pill(lv_obj_t* p_parent, size_t index);
    void update_pills();
    void update_header();
    void set_status(const char* p_text, uint32_t color);

    Action action_of(size_t index) const;
    size_t update_count() const;
    void act(size_t index);
    void start_next_update();
    void show_detail(size_t index);
    void load_shots();
    bool show_shot(uint8_t n);

    static void on_tab_discover(lv_event_t* p_event);
    static void on_tab_updates(lv_event_t* p_event);
    static void on_refresh(lv_event_t* p_event);
    static void on_card_clicked(lv_event_t* p_event);
    static void on_pill_clicked(lv_event_t* p_event);
    static void on_back_clicked(lv_event_t* p_event);
    static void on_update_all(lv_event_t* p_event);
    static void on_timer(lv_timer_t* p_timer);

    lv_obj_t* m_p_root;
    lv_obj_t* m_p_tab_discover;
    lv_obj_t* m_p_tab_updates;
    lv_obj_t* m_p_refresh;
    lv_obj_t* m_p_status;
    lv_obj_t* m_p_body;
    lv_obj_t* m_p_shot_row;                  /**< Hàng ảnh chụp (trang chi tiết / thẻ nổi bật). */
    lv_obj_t* m_p_shot_slots[MAX_SHOTS];     /**< Ô chờ ảnh (thay bằng ảnh khi đã tải). */
    static constexpr size_t MAX_PILLS = runtime::STORE_MAX_APPS + 1U; /**< Mỗi app một nút + nút thẻ nổi bật. */
    lv_obj_t* m_p_pills[MAX_PILLS]; /**< Nút hành động trên trang (user_data = chỉ số app). */
    size_t m_pill_count;
    lv_timer_t* m_p_timer;
    Page m_page;
    Page m_return_page;                      /**< Trang quay lại từ chi tiết. */
    size_t m_detail;                         /**< App đang xem chi tiết. */
    size_t m_busy_app;                       /**< App đang tải (STORE_MAX_APPS = không có). */
    uint8_t m_shot_pending;                  /**< Ảnh đang tải (0 = không). */
    uint8_t m_shot_owner_page;               /**< Ảnh trong bộ đệm thuộc trang nào (tránh vẽ ảnh cũ). */
    bool m_loaded;
    uint32_t m_retry_ticks; /**< Đếm nhịp timer từ lần tải danh mục lỗi (tự thử lại). */
    bool m_update_all;
    runtime::StoreState m_last_state;
};

} // namespace apps

#endif /* APPS_GACH_STORE_APP_HPP */
