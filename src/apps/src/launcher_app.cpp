/**
 * @file    launcher_app.cpp
 * @brief   Hiện thực home screen nhiều trang (lv_tileview, vuốt ngang).
 */
#include "launcher_app.hpp"

#include <cstdio>
#include <cstring>

#include "common/string_utils.hpp"
#include "framework/app_manager.hpp"
#include "framework/services.hpp"
#include "framework/system_ui.hpp"
#include "framework/theme.hpp"
#include "runtime/runtime.hpp"

namespace apps {

namespace {

constexpr fw::AppInfo INFO{"launcher", "Home", LV_SYMBOL_HOME, 0x2F81F7U, false};
// Kích thước theo dp (theme::px đổi sang pixel thật).
constexpr int32_t TILE_WIDTH = 88;   /**< dp: đủ cho từ dài như "Calculator" ở cỡ chữ nhỏ. */
constexpr int32_t NAME_LINES = 2;    /**< Tên tối đa 2 dòng (ngắt theo từ), dư thì "...". */
constexpr int32_t ICON_SIZE = 52;
constexpr int32_t ICON_RADIUS = 14;
constexpr int32_t GRID_GAP = 6;
constexpr int32_t TILE_ROW_GAP = 4;
constexpr int32_t DOT_SIZE = 6;
constexpr int32_t DOT_GAP = 6;
constexpr int32_t INDICATOR_HEIGHT = 12;
constexpr uint32_t DOT_ACTIVE = 0xE6EDF3U;
constexpr uint32_t DOT_INACTIVE = 0x484F58U;
constexpr uint32_t REGISTRY_CHECK_MS = 1000U; // App có thể được cài/gỡ khi launcher đang hiển thị.

// ---- Chế độ sửa ----
constexpr const char* ORDER_KEY = "home_order"; /**< NVS: id app theo thứ tự trên màn hình chính, cách nhau ','. */
constexpr size_t ORDER_BUF_LEN = 2048U;
// Rung = lắc ngang vài px (translate): xoay (transform_rotation) bắt LVGL vẽ từng icon ra layer riêng -> 6 fps,
// CPU 96 % với 15 icon (đo trên JC8012P4A1C); dịch chuyển không cần layer.
constexpr int32_t JIGGLE_PX = 2;              /**< dp, biên độ lắc. */
constexpr uint32_t JIGGLE_MS = 120U;
constexpr int32_t DRAG_THRESHOLD = 12;        /**< px: dịch quá ngưỡng này thì bắt đầu kéo. */
constexpr int32_t BADGE_SIZE = 24;            /**< dp */
constexpr int32_t BADGE_X = 8;                /**< dp, đè góc trên trái icon. */
constexpr uint32_t BADGE_COLOR = 0x48484AU;
constexpr int32_t DONE_PAD_X = 18;            /**< dp */
constexpr int32_t DONE_HEIGHT = 34;           /**< dp */
constexpr int32_t DONE_MARGIN = 8;            /**< dp */
constexpr uint32_t DONE_COLOR = 0x0A84FFU;
char s_order[ORDER_BUF_LEN];

void jiggle_cb(void* p_var, int32_t value)
{
    lv_obj_set_style_translate_x(static_cast<lv_obj_t*>(p_var), value, 0);
}

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
    // Chiều cao cố định 2 dòng: LONG_DOT chỉ cắt ("...") khi có chiều cao; mọi ô cao bằng nhau nên hàng thẳng.
    lv_style_set_text_font(&s_style_name, fw::theme::font_small());
    lv_style_set_height(&s_style_name, NAME_LINES * lv_font_get_line_height(fw::theme::font_small()));
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
      m_tiles_per_page(MIN_TILES_PER_PAGE), m_columns(1U), m_rows(1U), m_col_dsc{}, m_row_dsc{}, m_page_count(0U), m_active_page(0U), m_p_timer(nullptr),
      m_built_generation(0U), m_is_paused(false), m_is_editing(false), m_p_done(nullptr), m_p_drag(nullptr),
      m_p_dialog(nullptr), m_press_point{0, 0}, m_drag_offset{0, 0}, m_drag_from(0U), m_p_delete(nullptr)
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

    // Nút "Done" của chế độ sửa: nổi ở góc trên phải, ẩn khi không sửa.
    m_p_done = lv_obj_create(p_root);
    lv_obj_remove_style_all(m_p_done);
    lv_obj_set_floating(m_p_done, true);
    lv_obj_set_size(m_p_done, LV_SIZE_CONTENT, fw::theme::px(DONE_HEIGHT));
    lv_obj_set_style_pad_hor(m_p_done, fw::theme::px(DONE_PAD_X), 0);
    lv_obj_set_style_radius(m_p_done, LV_RADIUS_CIRCLE, 0);
    lv_obj_set_style_bg_color(m_p_done, lv_color_hex(DONE_COLOR), 0);
    lv_obj_set_style_bg_opa(m_p_done, LV_OPA_COVER, 0);
    lv_obj_set_style_bg_opa(m_p_done, LV_OPA_70, LV_STATE_PRESSED);
    lv_obj_align(m_p_done, LV_ALIGN_BOTTOM_RIGHT, -fw::theme::px(DONE_MARGIN), -fw::theme::px(DONE_MARGIN));
    lv_obj_set_clickable(m_p_done, true);
    lv_obj_add_event_cb(m_p_done, &LauncherApp::on_done_clicked, LV_EVENT_CLICKED, this);
    lv_obj_t* const p_done_text = lv_label_create(m_p_done);
    lv_label_set_text_static(p_done_text, "Done");
    lv_obj_set_style_text_color(p_done_text, lv_color_white(), 0);
    lv_obj_center(p_done_text);
    lv_obj_set_hidden(m_p_done, true);

    build_pages();
}

bool LauncherApp::on_back()
{
    if (m_p_dialog != nullptr) {
        close_dialog();
        return true;
    }
    if (m_is_editing) {
        exit_edit();
        return true;
    }
    return false;
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
    apply_saved_order();

    lv_obj_clean(m_p_tileview);
    m_tiles_per_page = compute_tiles_per_page();
    // Lưới: cột chia đều (icon căn giữa trong cột), hàng cao theo nội dung.
    for (size_t col = 0U; col < m_columns; ++col) {
        m_col_dsc[col] = LV_GRID_FR(1);
    }
    m_col_dsc[m_columns] = LV_GRID_TEMPLATE_LAST;
    for (size_t row = 0U; row < m_rows; ++row) {
        m_row_dsc[row] = LV_GRID_CONTENT;
    }
    m_row_dsc[m_rows] = LV_GRID_TEMPLATE_LAST;
    m_page_count = (m_visible_count == 0U) ? 1U : ((m_visible_count + m_tiles_per_page - 1U) / m_tiles_per_page);
    for (size_t page = 0U; page < m_page_count; ++page) {
        lv_obj_t* const p_page = lv_tileview_add_tile(m_p_tileview, static_cast<uint8_t>(page), 0U, LV_DIR_HOR);
        lv_obj_set_user_data(p_page, reinterpret_cast<void*>(static_cast<uintptr_t>(page)));
        lv_obj_set_style_pad_all(p_page, fw::theme::px(GRID_GAP), 0);
        lv_obj_set_style_pad_row(p_page, fw::theme::px(GRID_GAP), 0);
        lv_obj_set_style_pad_column(p_page, fw::theme::px(GRID_GAP), 0);
        // Lưới cột chia đều (không phải flex wrap): hàng thiếu icon vẫn thẳng cột với hàng trên.
        lv_obj_set_grid_dsc_array(p_page, m_col_dsc, m_row_dsc);
        lv_obj_set_layout(p_page, LV_LAYOUT_GRID);
        lv_obj_set_scrollable(p_page, false); // Chỉ tileview cuộn (ngang).
        lv_obj_set_clickable(p_page, true);   // Chạm chỗ trống khi đang sửa = xong.
        lv_obj_add_event_cb(p_page, &LauncherApp::on_page_clicked, LV_EVENT_CLICKED, this);
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

size_t LauncherApp::compute_tiles_per_page()
{
    // Diện tích lưới = tileview (đã layout) trừ lề trang; mỗi ô = icon + khoảng + 1 dòng tên.
    lv_obj_update_layout(m_p_tileview);
    const int32_t gap = fw::theme::px(GRID_GAP);
    const int32_t area_w = lv_obj_get_content_width(m_p_tileview) - gap;
    const int32_t area_h = lv_obj_get_content_height(m_p_tileview) - gap;
    const int32_t tile_w = fw::theme::px(TILE_WIDTH) + gap;
    const int32_t tile_h = fw::theme::px(ICON_SIZE) + fw::theme::px(TILE_ROW_GAP) +
                           (NAME_LINES * lv_font_get_line_height(fw::theme::font_small())) + gap;
    size_t cols = (area_w > 0) ? static_cast<size_t>(area_w / tile_w) : 0U;
    size_t rows = (area_h > 0) ? static_cast<size_t>(area_h / tile_h) : 0U;
    cols = (cols > MAX_GRID_COLUMNS) ? MAX_GRID_COLUMNS : ((cols == 0U) ? 1U : cols);
    rows = (rows > MAX_GRID_ROWS) ? MAX_GRID_ROWS : ((rows == 0U) ? 1U : rows);
    if ((cols * rows) < MIN_TILES_PER_PAGE) { // Màn quá nhỏ: giữ sàn số icon/trang (giới hạn số trang).
        rows = (MIN_TILES_PER_PAGE + cols - 1U) / cols;
        rows = (rows > MAX_GRID_ROWS) ? MAX_GRID_ROWS : rows;
        cols = (MIN_TILES_PER_PAGE + rows - 1U) / rows;
        cols = (cols > MAX_GRID_COLUMNS) ? MAX_GRID_COLUMNS : cols;
    }
    m_columns = cols;
    m_rows = rows;
    return cols * rows;
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
        create_tile(p_page, *m_visible[index], index - first);
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

void LauncherApp::create_tile(lv_obj_t* p_parent, fw::App& target, size_t slot)
{
    // Tile = container clickable; con của nó không clickable để click luôn rơi vào tile.
    lv_obj_t* const p_tile = lv_obj_create(p_parent);
    lv_obj_remove_style_all(p_tile);
    lv_obj_add_style(p_tile, &s_style_tile, 0);
    lv_obj_add_style(p_tile, &s_style_tile_pressed, LV_STATE_PRESSED);
    lv_obj_set_clickable(p_tile, true);
    lv_obj_set_user_data(p_tile, &target);
    lv_obj_add_event_cb(p_tile, &LauncherApp::on_tile_clicked, LV_EVENT_CLICKED, this);
    lv_obj_add_event_cb(p_tile, &LauncherApp::on_tile_event, LV_EVENT_ALL, this);
    lv_obj_set_grid_cell(p_tile, LV_GRID_ALIGN_CENTER, static_cast<int32_t>(slot % m_columns), 1, LV_GRID_ALIGN_START,
                         static_cast<int32_t>(slot / m_columns), 1);

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
    if (m_is_editing) {
        decorate(p_tile, target); // Dựng lại khi đang sửa (VD vừa xóa app): giữ rung + ×.
    }
}

void LauncherApp::on_tile_clicked(lv_event_t* p_event)
{
    auto* const p_self = static_cast<LauncherApp*>(lv_event_get_user_data(p_event));
    if (p_self->m_is_editing) {
        return; // Đang sửa: chạm icon không mở app.
    }
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

// ============================================================================
// Chế độ sửa: rung, xóa (×), kéo thả sắp xếp, lưu thứ tự
// ============================================================================
void LauncherApp::apply_saved_order()
{
    if (!common::is_ok(context().services.load_setting(ORDER_KEY, s_order, sizeof(s_order)))) {
        return; // Chưa sắp xếp lần nào: thứ tự đăng ký.
    }
    size_t placed = 0U;
    char* p_save = nullptr;
    for (char* p_id = strtok_r(s_order, ",", &p_save); (p_id != nullptr) && (placed < m_visible_count);
         p_id = strtok_r(nullptr, ",", &p_save)) {
        for (size_t index = placed; index < m_visible_count; ++index) {
            if (std::strcmp(m_visible[index]->info().p_id, p_id) == 0) {
                fw::App* const p_app = m_visible[index];
                for (size_t shift = index; shift > placed; --shift) { // Giữ thứ tự tương đối của phần còn lại.
                    m_visible[shift] = m_visible[shift - 1U];
                }
                m_visible[placed++] = p_app;
                break;
            }
        }
    }
    // App chưa có trong danh sách đã lưu (VD vừa cài) nằm cuối, theo thứ tự đăng ký.
}

void LauncherApp::save_order()
{
    size_t used = 0U;
    s_order[0] = '\0';
    for (size_t index = 0U; index < m_visible_count; ++index) {
        const int written = std::snprintf(&s_order[used], sizeof(s_order) - used, "%s%s", (index == 0U) ? "" : ",",
                                          m_visible[index]->info().p_id);
        if ((written < 0) || (static_cast<size_t>(written) >= (sizeof(s_order) - used))) {
            s_order[used] = '\0'; // Hết chỗ: phần còn lại giữ thứ tự đăng ký.
            break;
        }
        used += static_cast<size_t>(written);
    }
    (void)context().services.save_setting(ORDER_KEY, s_order);
}

void LauncherApp::decorate(lv_obj_t* p_tile, fw::App& target)
{
    lv_anim_t anim;
    lv_anim_init(&anim);
    lv_anim_set_var(&anim, p_tile);
    lv_anim_set_exec_cb(&anim, &jiggle_cb);
    const bool is_odd = ((lv_obj_get_index(p_tile) % 2) != 0); // So le như điện thoại.
    const int32_t amplitude = fw::theme::px(JIGGLE_PX);
    lv_anim_set_values(&anim, is_odd ? amplitude : -amplitude, is_odd ? -amplitude : amplitude);
    lv_anim_set_duration(&anim, JIGGLE_MS);
    lv_anim_set_reverse_duration(&anim, JIGGLE_MS);
    lv_anim_set_repeat_count(&anim, LV_ANIM_REPEAT_INFINITE);
    (void)lv_anim_start(&anim);

    runtime::PackageDetails details{};
    if (!runtime::package_details(target.info().p_id, details)) {
        return; // App tích hợp: không xóa được.
    }
    lv_obj_t* const p_badge = lv_obj_create(p_tile);
    lv_obj_remove_style_all(p_badge);
    lv_obj_set_floating(p_badge, true);
    const int32_t size = fw::theme::px(BADGE_SIZE);
    lv_obj_set_size(p_badge, size, size);
    lv_obj_set_pos(p_badge, fw::theme::px(BADGE_X), 0);
    lv_obj_set_style_radius(p_badge, LV_RADIUS_CIRCLE, 0);
    lv_obj_set_style_bg_color(p_badge, lv_color_hex(BADGE_COLOR), 0);
    lv_obj_set_style_bg_opa(p_badge, LV_OPA_COVER, 0);
    lv_obj_set_style_border_width(p_badge, fw::theme::px(1), 0);
    lv_obj_set_style_border_color(p_badge, lv_color_black(), 0);
    lv_obj_set_clickable(p_badge, true);
    lv_obj_set_ext_click_area(p_badge, size / 2); // Dễ chạm hơn kích thước hiển thị.
    lv_obj_set_user_data(p_badge, &target);
    lv_obj_add_event_cb(p_badge, &LauncherApp::on_badge_clicked, LV_EVENT_CLICKED, this);
    lv_obj_t* const p_x = lv_label_create(p_badge);
    lv_label_set_text_static(p_x, LV_SYMBOL_CLOSE);
    lv_obj_set_style_text_font(p_x, fw::theme::font_small(), 0);
    lv_obj_set_style_text_color(p_x, lv_color_white(), 0);
    lv_obj_center(p_x);
}

void LauncherApp::enter_edit()
{
    if (m_is_editing) {
        return;
    }
    m_is_editing = true;
    lv_obj_set_scrollable(m_p_tileview, false); // Kéo icon không lật trang.
    lv_obj_set_hidden(m_p_done, false);
    lv_obj_move_foreground(m_p_done);
    // Trang mở sẵn: thêm rung + × tại chỗ (không dựng lại: ngón tay vẫn đang giữ icon vừa nhấn).
    lv_obj_t* const p_page = m_pages[m_active_page];
    const uint32_t count = lv_obj_get_child_count(p_page);
    for (uint32_t index = 0U; index < count; ++index) {
        lv_obj_t* const p_tile = lv_obj_get_child(p_page, static_cast<int32_t>(index));
        auto* const p_app = static_cast<fw::App*>(lv_obj_get_user_data(p_tile));
        if (p_app != nullptr) {
            decorate(p_tile, *p_app);
        }
    }
}

void LauncherApp::exit_edit()
{
    if (!m_is_editing) {
        return;
    }
    m_is_editing = false;
    m_p_drag = nullptr;
    close_dialog();
    lv_obj_set_scrollable(m_p_tileview, true);
    lv_obj_set_hidden(m_p_done, true);
    (void)lv_async_call(&LauncherApp::rebuild_active, this); // Dựng lại icon sạch (bỏ rung, ×).
}

void LauncherApp::rebuild_active(void* p_arg)
{
    auto* const p_self = static_cast<LauncherApp*>(p_arg);
    if (p_self->m_is_paused || (p_self->m_active_page >= p_self->m_page_count)) {
        return;
    }
    lv_obj_t* const p_page = p_self->m_pages[p_self->m_active_page];
    const uint32_t count = lv_obj_get_child_count(p_page);
    for (uint32_t index = 0U; index < count; ++index) {
        lv_anim_delete(lv_obj_get_child(p_page, static_cast<int32_t>(index)), &jiggle_cb);
    }
    lv_obj_clean(p_page);
    p_self->populate(p_self->m_active_page);
}

void LauncherApp::begin_drag(lv_obj_t* p_tile)
{
    auto* const p_app = static_cast<fw::App*>(lv_obj_get_user_data(p_tile));
    size_t from = m_visible_count;
    for (size_t index = 0U; index < m_visible_count; ++index) {
        if (m_visible[index] == p_app) {
            from = index;
            break;
        }
    }
    if (from == m_visible_count) {
        return;
    }
    m_drag_from = from;
    m_p_drag = p_tile;
    lv_area_t coords;
    lv_obj_get_coords(p_tile, &coords);
    m_drag_offset = lv_point_t{m_press_point.x - coords.x1, m_press_point.y - coords.y1};
    lv_anim_delete(p_tile, &jiggle_cb);
    lv_obj_set_style_translate_x(p_tile, 0, 0);
    lv_obj_set_floating(p_tile, true); // Rời lưới, đi theo ngón tay.
    lv_obj_move_foreground(p_tile);
    lv_obj_set_style_opa(p_tile, LV_OPA_80, 0);
}

void LauncherApp::finish_drag()
{
    lv_obj_t* const p_page = m_pages[m_active_page];
    lv_point_t point{0, 0};
    if (lv_indev_active() != nullptr) {
        lv_indev_get_point(lv_indev_active(), &point);
    }
    lv_area_t page_area;
    lv_obj_get_coords(p_page, &page_area);
    const int32_t left = page_area.x1 + lv_obj_get_style_pad_left(p_page, LV_PART_MAIN);
    const int32_t top = page_area.y1 + lv_obj_get_style_pad_top(p_page, LV_PART_MAIN);
    const int32_t content_w = lv_obj_get_content_width(p_page);
    const int32_t row_h = lv_obj_get_height(m_p_drag) + lv_obj_get_style_pad_row(p_page, LV_PART_MAIN);
    const auto cols = static_cast<int32_t>(m_columns);
    int32_t col = ((point.x - left) * cols) / ((content_w > 0) ? content_w : 1);
    int32_t row = (point.y - top) / ((row_h > 0) ? row_h : 1);
    col = (col < 0) ? 0 : ((col >= cols) ? (cols - 1) : col);
    row = (row < 0) ? 0 : ((row >= static_cast<int32_t>(m_rows)) ? static_cast<int32_t>(m_rows) - 1 : row);
    const size_t first = m_active_page * m_tiles_per_page;
    const size_t last = (((first + m_tiles_per_page) < m_visible_count) ? (first + m_tiles_per_page) : m_visible_count) - 1U;
    size_t target = first + static_cast<size_t>((row * cols) + col);
    target = (target > last) ? last : target;

    fw::App* const p_app = m_visible[m_drag_from];
    if (target < m_drag_from) {
        for (size_t index = m_drag_from; index > target; --index) {
            m_visible[index] = m_visible[index - 1U];
        }
    } else {
        for (size_t index = m_drag_from; index < target; ++index) {
            m_visible[index] = m_visible[index + 1U];
        }
    }
    m_visible[target] = p_app;
    m_p_drag = nullptr;
    save_order();
    (void)lv_async_call(&LauncherApp::rebuild_active, this); // Không xóa icon ngay trong sự kiện của chính nó.
}

void LauncherApp::on_tile_event(lv_event_t* p_event)
{
    auto* const p_self = static_cast<LauncherApp*>(lv_event_get_user_data(p_event));
    lv_obj_t* const p_tile = lv_event_get_current_target_obj(p_event);
    const lv_event_code_t code = lv_event_get_code(p_event);
    lv_indev_t* const p_indev = lv_indev_active();
    lv_point_t point{0, 0};
    if (p_indev != nullptr) {
        lv_indev_get_point(p_indev, &point);
    }
    if (code == LV_EVENT_PRESSED) {
        p_self->m_press_point = point;
    } else if (code == LV_EVENT_LONG_PRESSED) {
        p_self->enter_edit();
    } else if ((code == LV_EVENT_PRESSING) && p_self->m_is_editing) {
        if (p_self->m_p_drag == nullptr) {
            const int32_t dx = point.x - p_self->m_press_point.x;
            const int32_t dy = point.y - p_self->m_press_point.y;
            if (((dx * dx) + (dy * dy)) > (DRAG_THRESHOLD * DRAG_THRESHOLD)) {
                p_self->begin_drag(p_tile);
            }
        }
        if (p_self->m_p_drag == p_tile) {
            lv_obj_t* const p_page = lv_obj_get_parent(p_tile);
            lv_area_t page_area;
            lv_obj_get_coords(p_page, &page_area);
            lv_obj_set_pos(p_tile, point.x - p_self->m_drag_offset.x - page_area.x1 - lv_obj_get_style_pad_left(p_page, LV_PART_MAIN),
                           point.y - p_self->m_drag_offset.y - page_area.y1 - lv_obj_get_style_pad_top(p_page, LV_PART_MAIN));
        }
    } else if (((code == LV_EVENT_RELEASED) || (code == LV_EVENT_PRESS_LOST)) && (p_self->m_p_drag == p_tile)) {
        p_self->finish_drag();
    } else {
        // Sự kiện khác: không xử lý.
    }
}

void LauncherApp::on_page_clicked(lv_event_t* p_event)
{
    auto* const p_self = static_cast<LauncherApp*>(lv_event_get_user_data(p_event));
    if (lv_event_get_target_obj(p_event) == lv_event_get_current_target_obj(p_event)) { // Chạm chỗ trống.
        p_self->exit_edit();
    }
}

void LauncherApp::on_done_clicked(lv_event_t* p_event)
{
    static_cast<LauncherApp*>(lv_event_get_user_data(p_event))->exit_edit();
}

void LauncherApp::ask_delete(fw::App& target)
{
    if (m_p_dialog != nullptr) {
        return;
    }
    m_p_delete = &target;
    char title[48];
    (void)std::snprintf(title, sizeof(title), "Delete \"%s\"?", target.info().p_name);
    m_p_dialog = fw::alert::show(title, "Deleting this app also deletes its data.",
                                 fw::alert::Action{"Cancel", fw::alert::COLOR_CANCEL, &LauncherApp::on_dialog_cancel},
                                 fw::alert::Action{"Delete", fw::alert::COLOR_DESTRUCTIVE, &LauncherApp::on_delete_confirm},
                                 this);
}

void LauncherApp::close_dialog()
{
    if (m_p_dialog != nullptr) {
        lv_msgbox_close_async(m_p_dialog);
        m_p_dialog = nullptr;
    }
}

void LauncherApp::on_badge_clicked(lv_event_t* p_event)
{
    auto* const p_self = static_cast<LauncherApp*>(lv_event_get_user_data(p_event));
    auto* const p_app = static_cast<fw::App*>(lv_obj_get_user_data(lv_event_get_current_target_obj(p_event)));
    if (p_app != nullptr) {
        p_self->ask_delete(*p_app);
    }
}

void LauncherApp::on_delete_confirm(lv_event_t* p_event)
{
    auto* const p_self = static_cast<LauncherApp*>(lv_event_get_user_data(p_event));
    char id[runtime::PACKAGE_ID_BUF_LEN];
    (void)common::copy_string(id, sizeof(id), (p_self->m_p_delete != nullptr) ? p_self->m_p_delete->info().p_id : "");
    p_self->m_p_delete = nullptr; // App sắp bị gỡ khỏi registry: không giữ con trỏ.
    p_self->close_dialog();
    (void)runtime::uninstall(id);
    p_self->refresh_if_changed(); // Dựng lại (vẫn ở chế độ sửa: icon mới được trang trí trong create_tile).
}

void LauncherApp::on_dialog_cancel(lv_event_t* p_event)
{
    static_cast<LauncherApp*>(lv_event_get_user_data(p_event))->close_dialog();
}

} // namespace apps
