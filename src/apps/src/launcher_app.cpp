/**
 * @file    launcher_app.cpp
 * @brief   Hiện thực home screen nhiều trang (lv_tileview, vuốt ngang).
 */
#include "launcher_app.hpp"

#include "framework/app_manager.hpp"
#include "framework/theme.hpp"

namespace apps {

namespace {

constexpr fw::AppInfo INFO{"launcher", "Home", LV_SYMBOL_HOME, 0x2F81F7U, false};
// Kích thước theo dp (theme::px đổi sang pixel thật).
constexpr int32_t TILE_WIDTH = 72;
constexpr int32_t ICON_SIZE = 52;
constexpr int32_t ICON_RADIUS = 14;
constexpr int32_t GRID_GAP = 6;
constexpr int32_t TILE_ROW_GAP = 4;
constexpr int32_t DOT_SIZE = 6;
constexpr int32_t DOT_GAP = 6;
constexpr int32_t INDICATOR_HEIGHT = 12;
constexpr size_t MAX_COLUMNS = 8U; /**< Màn rất rộng: giữ khoảng thở giữa các icon thay vì nhồi thêm cột. */
constexpr size_t MAX_ROWS = 4U;
constexpr uint32_t DOT_ACTIVE = 0xE6EDF3U;
constexpr uint32_t DOT_INACTIVE = 0x484F58U;
constexpr uint32_t REGISTRY_CHECK_MS = 1000U; // App có thể được cài/gỡ khi launcher đang hiển thị.

// Style dùng chung cho mọi icon: rẻ hơn nhiều so với style cục bộ trên từng object.
lv_style_t s_style_tile;
lv_style_t s_style_tile_pressed;
lv_style_t s_style_icon;
lv_style_t s_style_symbol;
lv_style_t s_style_name;
bool s_styles_ready = false;

void init_styles()
{
    if (s_styles_ready) {
        return;
    }
    lv_style_init(&s_style_tile);
    lv_style_set_width(&s_style_tile, fw::theme::px(TILE_WIDTH));
    lv_style_set_height(&s_style_tile, LV_SIZE_CONTENT);
    lv_style_set_layout(&s_style_tile, LV_LAYOUT_FLEX);
    lv_style_set_flex_flow(&s_style_tile, LV_FLEX_FLOW_COLUMN);
    lv_style_set_flex_cross_place(&s_style_tile, LV_FLEX_ALIGN_CENTER);
    lv_style_set_pad_row(&s_style_tile, fw::theme::px(TILE_ROW_GAP));

    lv_style_init(&s_style_tile_pressed);
    lv_style_set_opa(&s_style_tile_pressed, LV_OPA_60);

    lv_style_init(&s_style_icon);
    lv_style_set_width(&s_style_icon, fw::theme::px(ICON_SIZE));
    lv_style_set_height(&s_style_icon, fw::theme::px(ICON_SIZE));
    lv_style_set_radius(&s_style_icon, fw::theme::px(ICON_RADIUS));
    lv_style_set_bg_opa(&s_style_icon, LV_OPA_COVER);

    lv_style_init(&s_style_symbol);
    lv_style_set_text_font(&s_style_symbol, fw::theme::font_medium());
    lv_style_set_text_color(&s_style_symbol, lv_color_white());
    lv_style_set_align(&s_style_symbol, LV_ALIGN_CENTER);

    lv_style_init(&s_style_name);
    lv_style_set_width(&s_style_name, fw::theme::px(TILE_WIDTH));
    lv_style_set_text_align(&s_style_name, LV_TEXT_ALIGN_CENTER);
    lv_style_set_text_color(&s_style_name, lv_color_hex(fw::theme::COLOR_TEXT));
    s_styles_ready = true;
}

size_t page_of(lv_obj_t* p_page)
{
    return static_cast<size_t>(reinterpret_cast<uintptr_t>(lv_obj_get_user_data(p_page)));
}

} // namespace

LauncherApp::LauncherApp()
    : fw::App(INFO), m_p_tileview(nullptr), m_p_indicator(nullptr), m_pages{}, m_visible{}, m_visible_count(0U),
      m_tiles_per_page(MIN_TILES_PER_PAGE), m_page_count(0U), m_active_page(0U), m_p_timer(nullptr),
      m_built_generation(0U), m_is_paused(false)
{
}

void LauncherApp::on_create(lv_obj_t* p_root)
{
    init_styles();
    lv_obj_set_flex_flow(p_root, LV_FLEX_FLOW_COLUMN);
    lv_obj_set_scrollable(p_root, false);

    m_p_tileview = lv_tileview_create(p_root);
    lv_obj_remove_style_all(m_p_tileview);
    lv_obj_set_width(m_p_tileview, LV_PCT(100));
    lv_obj_set_flex_grow(m_p_tileview, 1);
    lv_obj_set_scrollbar_mode(m_p_tileview, LV_SCROLLBAR_MODE_OFF);
    lv_obj_add_event_cb(m_p_tileview, &LauncherApp::on_page_changed, LV_EVENT_VALUE_CHANGED, this);
    lv_obj_add_event_cb(m_p_tileview, &LauncherApp::on_scroll_begin, LV_EVENT_SCROLL_BEGIN, this);
    lv_obj_add_event_cb(m_p_tileview, &LauncherApp::on_page_changed, LV_EVENT_SCROLL_END, this);

    m_p_indicator = lv_obj_create(p_root);
    lv_obj_remove_style_all(m_p_indicator);
    lv_obj_set_size(m_p_indicator, LV_PCT(100), fw::theme::px(INDICATOR_HEIGHT));
    lv_obj_set_flex_flow(m_p_indicator, LV_FLEX_FLOW_ROW);
    lv_obj_set_flex_align(m_p_indicator, LV_FLEX_ALIGN_CENTER, LV_FLEX_ALIGN_CENTER, LV_FLEX_ALIGN_CENTER);
    lv_obj_set_style_pad_column(m_p_indicator, fw::theme::px(DOT_GAP), 0);
    lv_obj_set_clickable(m_p_indicator, false);

    build_pages();
}

void LauncherApp::on_resume()
{
    m_is_paused = false;
    if (context().apps.registry_generation() != m_built_generation) {
        build_pages();
    } else {
        show_page(m_active_page); // Dựng lại icon đã giải phóng khi bị che.
    }
    m_p_timer = lv_timer_create(&LauncherApp::on_timer, REGISTRY_CHECK_MS, this);
}

void LauncherApp::on_pause()
{
    m_is_paused = true;
    if (m_p_timer != nullptr) {
        lv_timer_delete(m_p_timer);
        m_p_timer = nullptr;
    }
    // Bị app khác che: trả RAM LVGL cho app đó. Hoãn lại vì có thể đang ở trong callback chạm icon.
    (void)lv_async_call(&LauncherApp::release_all, this);
}

void LauncherApp::release_all(void* p_arg)
{
    auto* const p_self = static_cast<LauncherApp*>(p_arg);
    if (!p_self->m_is_paused) {
        return; // Đã quay lại launcher trước khi kịp giải phóng.
    }
    for (size_t index = 0U; index < p_self->m_page_count; ++index) {
        lv_obj_clean(p_self->m_pages[index]);
    }
}

void LauncherApp::refresh_if_changed()
{
    // Danh sách app đổi (cài/gỡ) -> dựng lại (tile giữ con trỏ App, không được để treo).
    if (context().apps.registry_generation() != m_built_generation) {
        build_pages();
    }
}

void LauncherApp::build_pages()
{
    fw::AppManager& apps = context().apps;
    m_visible_count = 0U;
    for (size_t index = 0U; index < apps.count(); ++index) {
        fw::App* const p_app = apps.at(index);
        if ((p_app != nullptr) && p_app->info().show_in_launcher && apps.is_enabled(p_app->info().p_id)) {
            m_visible[m_visible_count++] = p_app;
        }
    }
    m_built_generation = apps.registry_generation();

    lv_obj_clean(m_p_tileview);
    m_tiles_per_page = compute_tiles_per_page();
    m_page_count = (m_visible_count == 0U) ? 1U : ((m_visible_count + m_tiles_per_page - 1U) / m_tiles_per_page);
    for (size_t page = 0U; page < m_page_count; ++page) {
        lv_obj_t* const p_page = lv_tileview_add_tile(m_p_tileview, static_cast<uint8_t>(page), 0U, LV_DIR_HOR);
        lv_obj_set_user_data(p_page, reinterpret_cast<void*>(static_cast<uintptr_t>(page)));
        lv_obj_set_style_pad_all(p_page, fw::theme::px(GRID_GAP), 0);
        lv_obj_set_style_pad_row(p_page, fw::theme::px(GRID_GAP), 0);
        lv_obj_set_style_pad_column(p_page, fw::theme::px(GRID_GAP), 0);
        lv_obj_set_flex_flow(p_page, LV_FLEX_FLOW_ROW_WRAP);
        lv_obj_set_flex_align(p_page, LV_FLEX_ALIGN_SPACE_EVENLY, LV_FLEX_ALIGN_START, LV_FLEX_ALIGN_START);
        lv_obj_set_scrollable(p_page, false); // Chỉ tileview cuộn (ngang).
        m_pages[page] = p_page;
    }

    // Dấu chấm báo trang (ẩn khi chỉ có một trang).
    lv_obj_clean(m_p_indicator);
    for (size_t page = 0U; (m_page_count > 1U) && (page < m_page_count); ++page) {
        lv_obj_t* const p_dot = lv_obj_create(m_p_indicator);
        lv_obj_remove_style_all(p_dot);
        lv_obj_set_size(p_dot, fw::theme::px(DOT_SIZE), fw::theme::px(DOT_SIZE));
        lv_obj_set_style_radius(p_dot, LV_RADIUS_CIRCLE, 0);
        lv_obj_set_style_bg_opa(p_dot, LV_OPA_COVER, 0);
    }

    const size_t page = (m_active_page < m_page_count) ? m_active_page : (m_page_count - 1U);
    lv_tileview_set_tile_by_index(m_p_tileview, static_cast<uint32_t>(page), 0U, LV_ANIM_OFF);
    show_page(page);
}

size_t LauncherApp::compute_tiles_per_page() const
{
    // Diện tích lưới = tileview (đã layout) trừ lề trang; mỗi ô = icon + khoảng + 1 dòng tên.
    lv_obj_update_layout(m_p_tileview);
    const int32_t gap = fw::theme::px(GRID_GAP);
    const int32_t area_w = lv_obj_get_content_width(m_p_tileview) - gap;
    const int32_t area_h = lv_obj_get_content_height(m_p_tileview) - gap;
    const int32_t tile_w = fw::theme::px(TILE_WIDTH) + gap;
    const int32_t tile_h = fw::theme::px(ICON_SIZE) + fw::theme::px(TILE_ROW_GAP) +
                           lv_font_get_line_height(fw::theme::font_small()) + gap;
    size_t cols = (area_w > 0) ? static_cast<size_t>(area_w / tile_w) : 0U;
    size_t rows = (area_h > 0) ? static_cast<size_t>(area_h / tile_h) : 0U;
    cols = (cols > MAX_COLUMNS) ? MAX_COLUMNS : cols;
    rows = (rows > MAX_ROWS) ? MAX_ROWS : rows;
    const size_t tiles = cols * rows;
    return (tiles < MIN_TILES_PER_PAGE) ? MIN_TILES_PER_PAGE : tiles;
}

void LauncherApp::show_page(size_t page)
{
    // Đứng yên: chỉ giữ icon của trang đang xem (8 icon), giải phóng mọi trang khác.
    m_active_page = page;
    for (size_t index = 0U; index < m_page_count; ++index) {
        if (index == page) {
            populate(index);
        } else {
            lv_obj_clean(m_pages[index]);
        }
    }
    update_indicator();
}

void LauncherApp::populate(size_t page)
{
    lv_obj_t* const p_page = m_pages[page];
    if (lv_obj_get_child_count(p_page) > 0U) {
        return; // Đã dựng.
    }
    const size_t first = page * m_tiles_per_page;
    for (size_t index = first; (index < m_visible_count) && (index < (first + m_tiles_per_page)); ++index) {
        create_tile(p_page, *m_visible[index]);
    }
}

void LauncherApp::update_indicator()
{
    const uint32_t count = lv_obj_get_child_count(m_p_indicator);
    for (uint32_t index = 0U; index < count; ++index) {
        const uint32_t color = (index == m_active_page) ? DOT_ACTIVE : DOT_INACTIVE;
        lv_obj_set_style_bg_color(lv_obj_get_child(m_p_indicator, static_cast<int32_t>(index)), lv_color_hex(color),
                                  0);
    }
}

void LauncherApp::create_tile(lv_obj_t* p_parent, fw::App& target)
{
    // Tile = container clickable; con của nó không clickable để click luôn rơi vào tile.
    lv_obj_t* const p_tile = lv_obj_create(p_parent);
    lv_obj_remove_style_all(p_tile);
    lv_obj_add_style(p_tile, &s_style_tile, 0);
    lv_obj_add_style(p_tile, &s_style_tile_pressed, LV_STATE_PRESSED);
    lv_obj_set_clickable(p_tile, true);
    lv_obj_set_user_data(p_tile, &target);
    lv_obj_add_event_cb(p_tile, &LauncherApp::on_tile_clicked, LV_EVENT_CLICKED, this);

    lv_obj_t* const p_icon = lv_obj_create(p_tile);
    lv_obj_remove_style_all(p_icon);
    lv_obj_add_style(p_icon, &s_style_icon, 0);
    lv_obj_set_style_bg_color(p_icon, lv_color_hex(target.info().color_hex), 0); // Style cục bộ duy nhất.
    lv_obj_set_clickable(p_icon, false);

    // Ký hiệu: trỏ thẳng vào chuỗi của AppInfo (sống bằng đời app), không cấp phát bản sao.
    lv_obj_t* const p_symbol = lv_label_create(p_icon);
    lv_obj_remove_style_all(p_symbol);
    lv_obj_add_style(p_symbol, &s_style_symbol, 0);
    lv_label_set_text_static(p_symbol, target.info().p_icon);

    lv_obj_t* const p_name = lv_label_create(p_tile);
    lv_obj_remove_style_all(p_name);
    lv_obj_add_style(p_name, &s_style_name, 0);
    lv_label_set_long_mode(p_name, LV_LABEL_LONG_DOT);
    lv_label_set_text(p_name, target.info().p_name); // Bản sao: chế độ DOT ghi "..." vào bộ đệm chữ.
}

void LauncherApp::on_tile_clicked(lv_event_t* p_event)
{
    auto* const p_self = static_cast<LauncherApp*>(lv_event_get_user_data(p_event));
    auto* const p_target = static_cast<fw::App*>(lv_obj_get_user_data(lv_event_get_current_target_obj(p_event)));
    if (p_target != nullptr) {
        (void)p_self->context().apps.launch(p_target->info().p_id);
    }
}

void LauncherApp::on_scroll_begin(lv_event_t* p_event)
{
    // Bắt đầu vuốt: chỉ dựng trang đích theo hướng ngón tay (đỉnh RAM = 2 trang thay vì 3).
    auto* const p_self = static_cast<LauncherApp*>(lv_event_get_user_data(p_event));
    lv_indev_t* const p_indev = lv_indev_active();
    lv_point_t vect{0, 0};
    if (p_indev != nullptr) {
        lv_indev_get_vect(p_indev, &vect);
    }
    const size_t page = p_self->m_active_page;
    const bool to_next = (vect.x < 0);   // Ngón tay sang trái -> trang sau.
    const bool to_prev = (vect.x > 0);
    if ((to_next || (vect.x == 0)) && ((page + 1U) < p_self->m_page_count)) {
        p_self->populate(page + 1U);
    }
    if ((to_prev || (vect.x == 0)) && (page > 0U)) {
        p_self->populate(page - 1U);
    }
}

void LauncherApp::on_page_changed(lv_event_t* p_event)
{
    auto* const p_self = static_cast<LauncherApp*>(lv_event_get_user_data(p_event));
    if (lv_obj_is_scrolling(p_self->m_p_tileview)) {
        return; // Đang chạy hiệu ứng canh trang: đợi lần SCROLL_END cuối.
    }
    lv_obj_t* const p_active = lv_tileview_get_tile_active(p_self->m_p_tileview);
    if (p_active != nullptr) {
        p_self->show_page(page_of(p_active)); // Cũng giải phóng trang đích khi vuốt dở rồi bật lại.
    }
}

void LauncherApp::on_timer(lv_timer_t* p_timer)
{
    static_cast<LauncherApp*>(lv_timer_get_user_data(p_timer))->refresh_if_changed();
}

} // namespace apps
