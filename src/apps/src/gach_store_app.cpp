/**
 * @file    gach_store_app.cpp
 * @brief   Gach Store: giao diện kho ứng dụng (Khám phá / Cập nhật / Chi tiết) trên runtime::store_*.
 *
 * Mọi việc mạng chạy nền (runtime::store_*, task HTTP của platform); app hỏi tiến trình bằng lv_timer khi đang mở.
 * Ảnh chụp tải lần lượt từng ảnh, đệm trên thẻ SD (store_cache/), đọc vào bộ đệm tĩnh (PSRAM) để vẽ.
 */
#include "gach_store_app.hpp"

#include <cstdio>
#include <cstring>

#include "common/string_utils.hpp"
#include "framework/app_manager.hpp"
#include "framework/system_ui.hpp"
#include "framework/theme.hpp"
#include "runtime/sdk_version.hpp"
#include "ui_helpers.hpp"

namespace apps {

namespace {

constexpr fw::AppInfo INFO{"store", "Gach Store", LV_SYMBOL_DOWNLOAD, 0x0A84FFU, true, false};
constexpr size_t NONE = runtime::STORE_MAX_APPS;
constexpr uint32_t POLL_MS = 200U;
constexpr size_t TEXT_LEN = 128U;
constexpr uint32_t KIB = 1024U;
constexpr size_t PERCENT = 100U;
constexpr uint32_t RETRY_TICKS = 5000U / POLL_MS; /**< Chưa tải được danh mục (VD Wi-Fi chưa kết nối): thử lại mỗi 5 s. */

// ---- Kích thước (dp) ----
constexpr int32_t PAGE_PAD = 16;
constexpr int32_t GAP = 12;
constexpr int32_t CARD_RADIUS = 18;
constexpr int32_t CARD_PAD = 14;
constexpr int32_t ICON_ROW = 56;
constexpr int32_t ICON_FEATURED = 72;
constexpr int32_t ICON_DETAIL = 96;
constexpr int32_t ICON_RADIUS_DIV = 4; /**< Bo góc icon = cạnh / 4 (squircle mềm). */
constexpr int32_t PILL_W = 84;
constexpr int32_t PILL_H = 32;
constexpr int32_t TAB_H = 36;
constexpr int32_t TAB_PAD_X = 16;
constexpr int32_t SHOT_RADIUS = 14;
constexpr int32_t SHOT_SLOT_W = 284; /**< Ảnh chụp 426x266 px (màn 150 %). */
constexpr int32_t SHOT_SLOT_H = 177;
constexpr int32_t HALF_WIDTH_PCT = 49;

// ---- Màu ----
constexpr uint32_t COLOR_BG = 0x000000U;
constexpr uint32_t COLOR_CARD = fw::palette::SURFACE;
constexpr uint32_t COLOR_CARD_PRESSED = fw::palette::SURFACE_HIGH;
constexpr uint32_t COLOR_PILL_SECONDARY = fw::palette::FILL;
constexpr uint32_t COLOR_SEPARATOR = 0x2C2C2EU;

// ---- Ảnh chụp: bộ đệm tĩnh (apps .bss nằm trong PSRAM) ----
constexpr size_t SHOT_BUF_BYTES = 16U + (432U * 272U * 3U); /**< Header + RGB565A8 tối đa ~432x272. */
uint8_t s_shot_buf[GachStoreApp::MAX_SHOTS][SHOT_BUF_BYTES];
lv_image_dsc_t s_shot_dsc[GachStoreApp::MAX_SHOTS];

lv_obj_t* box(lv_obj_t* p_parent)
{
    lv_obj_t* const p_box = lv_obj_create(p_parent);
    lv_obj_remove_style_all(p_box);
    lv_obj_set_scrollable(p_box, false);
    lv_obj_set_clickable(p_box, false);
    return p_box;
}

lv_obj_t* flex(lv_obj_t* p_parent, lv_flex_flow_t flow, int32_t gap_dp)
{
    lv_obj_t* const p_box = box(p_parent);
    lv_obj_set_size(p_box, LV_PCT(100), LV_SIZE_CONTENT);
    lv_obj_set_flex_flow(p_box, flow);
    lv_obj_set_style_pad_gap(p_box, fw::theme::px(gap_dp), 0);
    return p_box;
}

lv_obj_t* text(lv_obj_t* p_parent, const char* p_text, const lv_font_t* p_font, uint32_t color)
{
    lv_obj_t* const p_label = lv_label_create(p_parent);
    lv_label_set_text(p_label, p_text);
    lv_obj_set_style_text_font(p_label, p_font, 0);
    lv_obj_set_style_text_color(p_label, lv_color_hex(color), 0);
    return p_label;
}

void card_style(lv_obj_t* p_card)
{
    lv_obj_set_style_bg_color(p_card, lv_color_hex(COLOR_CARD), 0);
    lv_obj_set_style_bg_opa(p_card, LV_OPA_COVER, 0);
    lv_obj_set_style_bg_color(p_card, lv_color_hex(COLOR_CARD_PRESSED), LV_STATE_PRESSED);
    lv_obj_set_style_radius(p_card, fw::theme::px(CARD_RADIUS), 0);
    lv_obj_set_style_pad_all(p_card, fw::theme::px(CARD_PAD), 0);
}

void fmt_size(uint32_t bytes, char* p_out, size_t len)
{
    if (bytes >= (KIB * KIB)) {
        (void)std::snprintf(p_out, len, "%.1f MB", static_cast<double>(bytes) / (KIB * KIB));
    } else {
        (void)std::snprintf(p_out, len, "%u KB", static_cast<unsigned>((bytes + KIB - 1U) / KIB));
    }
}

bool is_busy(runtime::StoreState state)
{
    return (state == runtime::StoreState::LOADING_INDEX) || (state == runtime::StoreState::DOWNLOADING) ||
           (state == runtime::StoreState::FETCHING_SHOT);
}

} // namespace

GachStoreApp::GachStoreApp()
    : fw::App(INFO), m_p_root(nullptr), m_p_tab_discover(nullptr), m_p_tab_updates(nullptr), m_p_refresh(nullptr),
      m_p_status(nullptr), m_p_body(nullptr), m_p_shot_row(nullptr), m_p_shot_slots{}, m_p_pills{}, m_pill_count(0U),
      m_p_timer(nullptr), m_page(Page::DISCOVER), m_return_page(Page::DISCOVER), m_detail(0U), m_busy_app(NONE),
      m_shot_pending(0U), m_shot_owner_page(0U), m_loaded(false), m_retry_ticks(0U), m_update_all(false),
      m_last_state(runtime::StoreState::IDLE)
{
}

// ============================================================================
// Vòng đời
// ============================================================================
void GachStoreApp::on_create(lv_obj_t* p_root)
{
    m_p_root = p_root;
    lv_obj_set_style_bg_color(p_root, lv_color_hex(COLOR_BG), 0);
    lv_obj_set_style_bg_opa(p_root, LV_OPA_COVER, 0);
    lv_obj_set_flex_flow(p_root, LV_FLEX_FLOW_COLUMN);
    lv_obj_set_style_pad_all(p_root, 0, 0);
    lv_obj_set_style_pad_gap(p_root, 0, 0);

    // ---- Đầu trang: tiêu đề, tab, trạng thái, làm mới ----
    lv_obj_t* const p_header = flex(p_root, LV_FLEX_FLOW_ROW, GAP);
    lv_obj_set_style_pad_hor(p_header, fw::theme::px(PAGE_PAD), 0);
    lv_obj_set_style_pad_ver(p_header, fw::theme::px(GAP), 0);
    lv_obj_set_flex_align(p_header, LV_FLEX_ALIGN_START, LV_FLEX_ALIGN_CENTER, LV_FLEX_ALIGN_CENTER);
    text(p_header, "Gach Store", fw::theme::font_large(), fw::palette::LABEL);

    const auto make_tab = [p_header, this](const char* p_label, lv_event_cb_t cb) {
        lv_obj_t* const p_tab = box(p_header);
        lv_obj_set_size(p_tab, LV_SIZE_CONTENT, fw::theme::px(TAB_H));
        lv_obj_set_style_pad_hor(p_tab, fw::theme::px(TAB_PAD_X), 0);
        lv_obj_set_style_radius(p_tab, LV_RADIUS_CIRCLE, 0);
        lv_obj_set_clickable(p_tab, true);
        lv_obj_add_event_cb(p_tab, cb, LV_EVENT_CLICKED, this);
        lv_obj_t* const p_text = text(p_tab, p_label, fw::theme::font_small(), fw::palette::LABEL);
        lv_obj_center(p_text);
        return p_tab;
    };
    m_p_tab_discover = make_tab(LV_SYMBOL_HOME "  Discover", &GachStoreApp::on_tab_discover);
    m_p_tab_updates = make_tab(LV_SYMBOL_REFRESH "  Updates", &GachStoreApp::on_tab_updates);

    m_p_status = text(p_header, "", fw::theme::font_small(), fw::palette::LABEL_SECONDARY);
    lv_obj_set_flex_grow(m_p_status, 1);
    lv_label_set_long_mode(m_p_status, LV_LABEL_LONG_DOT);
    lv_obj_set_style_text_align(m_p_status, LV_TEXT_ALIGN_RIGHT, 0);

    m_p_refresh = ui::button(p_header, LV_SYMBOL_REFRESH, &GachStoreApp::on_refresh, this);
    lv_obj_set_size(m_p_refresh, fw::theme::px(TAB_H), fw::theme::px(TAB_H));

    // ---- Thân trang (cuộn dọc) ----
    m_p_body = box(p_root);
    lv_obj_set_width(m_p_body, LV_PCT(100));
    lv_obj_set_flex_grow(m_p_body, 1);
    lv_obj_set_flex_flow(m_p_body, LV_FLEX_FLOW_COLUMN);
    lv_obj_set_style_pad_hor(m_p_body, fw::theme::px(PAGE_PAD), 0);
    lv_obj_set_style_pad_bottom(m_p_body, fw::theme::px(PAGE_PAD), 0);
    lv_obj_set_style_pad_gap(m_p_body, fw::theme::px(GAP), 0);
    lv_obj_set_scrollable(m_p_body, true);
    lv_obj_set_scroll_dir(m_p_body, LV_DIR_VER);
}

void GachStoreApp::on_resume()
{
    if (m_p_timer == nullptr) {
        m_p_timer = lv_timer_create(&GachStoreApp::on_timer, POLL_MS, this);
    }
    if (!m_loaded && !is_busy(m_last_state) && common::is_ok(runtime::store_refresh())) {
        m_last_state = runtime::StoreState::LOADING_INDEX;
    }
    render();
}

void GachStoreApp::on_pause()
{
    if (m_p_timer != nullptr) { // Việc tải vẫn chạy nền; kết quả xử lý ở lần mở sau.
        lv_timer_delete(m_p_timer);
        m_p_timer = nullptr;
    }
}

bool GachStoreApp::on_back()
{
    if (m_page == Page::DETAIL) {
        m_page = m_return_page;
        render();
        return true;
    }
    return false;
}

// ============================================================================
// Trạng thái app trong kho
// ============================================================================
GachStoreApp::Action GachStoreApp::action_of(size_t index) const
{
    const runtime::StoreApp& app = runtime::store_app(index);
    if (app.sdk > runtime::SDK_VERSION) {
        return Action::UNAVAILABLE;
    }
    runtime::PackageDetails installed{};
    if (!runtime::package_details(app.id, installed)) {
        return Action::GET;
    }
    return (std::strcmp(installed.p_version, app.version) != 0) ? Action::UPDATE : Action::OPEN;
}

size_t GachStoreApp::update_count() const
{
    size_t count = 0U;
    for (size_t index = 0U; index < runtime::store_count(); ++index) {
        count += (action_of(index) == Action::UPDATE) ? 1U : 0U;
    }
    return count;
}

void GachStoreApp::act(size_t index)
{
    const Action action = action_of(index);
    if (action == Action::OPEN) {
        (void)context().apps.launch(runtime::store_app(index).id);
        return;
    }
    if ((action == Action::UNAVAILABLE) || (m_busy_app != NONE)) {
        return;
    }
    if (!common::is_ok(runtime::store_install(index))) {
        set_status((runtime::store_message()[0] != '\0') ? runtime::store_message() : "Busy, try again in a moment",
                   fw::palette::DESTRUCTIVE);
        return;
    }
    m_busy_app = index;
    m_last_state = runtime::StoreState::DOWNLOADING;
    update_pills();
}

void GachStoreApp::start_next_update()
{
    for (size_t index = 0U; index < runtime::store_count(); ++index) {
        if (action_of(index) == Action::UPDATE) {
            act(index);
            return;
        }
    }
    m_update_all = false;
}

// ============================================================================
// Dựng trang
// ============================================================================
void GachStoreApp::render()
{
    lv_obj_clean(m_p_body);
    lv_obj_scroll_to_y(m_p_body, 0, LV_ANIM_OFF);
    m_p_shot_row = nullptr;
    for (lv_obj_t*& p_slot : m_p_shot_slots) {
        p_slot = nullptr;
    }
    m_pill_count = 0U;
    update_header();
    if (m_last_state == runtime::StoreState::LOADING_INDEX) {
        render_message(LV_SYMBOL_REFRESH, "Loading the store...", false);
    } else if (!m_loaded) {
        render_message(LV_SYMBOL_WARNING,
                       (runtime::store_message()[0] != '\0') ? runtime::store_message() : "Store not loaded", true);
    } else if (m_page == Page::DETAIL) {
        render_detail();
    } else if (m_page == Page::UPDATES) {
        render_updates();
    } else {
        render_discover();
    }
    load_shots();
}

void GachStoreApp::update_header()
{
    const bool on_updates = (m_page == Page::UPDATES);
    lv_obj_t* const tabs[] = {m_p_tab_discover, m_p_tab_updates};
    for (lv_obj_t* const p_tab : tabs) {
        const bool is_active = (p_tab == m_p_tab_updates) ? on_updates : (m_page == Page::DISCOVER);
        lv_obj_set_style_bg_color(p_tab, lv_color_hex(fw::palette::SURFACE_HIGH), 0);
        lv_obj_set_style_bg_opa(p_tab, is_active ? LV_OPA_COVER : LV_OPA_TRANSP, 0);
        lv_obj_set_style_text_color(lv_obj_get_child(p_tab, 0),
                                    lv_color_hex(is_active ? fw::palette::LABEL : fw::palette::LABEL_SECONDARY), 0);
    }
    char label[32];
    const size_t updates = m_loaded ? update_count() : 0U;
    if (updates > 0U) {
        (void)std::snprintf(label, sizeof(label), LV_SYMBOL_REFRESH "  Updates  %u", static_cast<unsigned>(updates));
    } else {
        (void)common::copy_string(label, sizeof(label), LV_SYMBOL_REFRESH "  Updates");
    }
    lv_label_set_text(lv_obj_get_child(m_p_tab_updates, 0), label);
}

void GachStoreApp::set_status(const char* p_text, uint32_t color)
{
    lv_label_set_text(m_p_status, p_text);
    lv_obj_set_style_text_color(m_p_status, lv_color_hex(color), 0);
}

void GachStoreApp::render_message(const char* p_symbol, const char* p_text, bool show_retry)
{
    lv_obj_t* const p_col = flex(m_p_body, LV_FLEX_FLOW_COLUMN, GAP);
    lv_obj_set_flex_align(p_col, LV_FLEX_ALIGN_CENTER, LV_FLEX_ALIGN_CENTER, LV_FLEX_ALIGN_CENTER);
    lv_obj_set_flex_grow(p_col, 1);
    lv_obj_set_height(p_col, LV_PCT(100));
    text(p_col, p_symbol, fw::theme::font_large(), fw::palette::LABEL_SECONDARY);
    lv_obj_t* const p_msg = text(p_col, p_text, fw::theme::font_medium(), fw::palette::LABEL_SECONDARY);
    lv_obj_set_style_text_align(p_msg, LV_TEXT_ALIGN_CENTER, 0);
    if (show_retry) {
        (void)ui::button(p_col, "Try again", &GachStoreApp::on_refresh, this);
    }
}

lv_obj_t* GachStoreApp::add_section_title(lv_obj_t* p_parent, const char* p_text)
{
    lv_obj_t* const p_title = text(p_parent, p_text, fw::theme::font_medium(), fw::palette::LABEL);
    lv_obj_set_style_pad_top(p_title, fw::theme::px(GAP / 2), 0);
    return p_title;
}

lv_obj_t* GachStoreApp::add_icon(lv_obj_t* p_parent, const runtime::StoreApp& app, int32_t size_dp)
{
    lv_obj_t* const p_icon = box(p_parent);
    const int32_t size = fw::theme::px(size_dp);
    lv_obj_set_size(p_icon, size, size);
    lv_obj_set_style_radius(p_icon, size / ICON_RADIUS_DIV, 0);
    lv_obj_set_style_bg_color(p_icon, lv_color_hex(app.color), 0);
    lv_obj_set_style_bg_opa(p_icon, LV_OPA_COVER, 0);
    lv_obj_t* const p_symbol = text(p_icon, runtime::app_icon_symbol(app.icon),
                                    (size_dp >= ICON_FEATURED) ? fw::theme::font_large() : fw::theme::font_medium(),
                                    fw::palette::LABEL);
    lv_obj_center(p_symbol);
    return p_icon;
}

lv_obj_t* GachStoreApp::add_pill(lv_obj_t* p_parent, size_t index)
{
    lv_obj_t* const p_pill = box(p_parent);
    lv_obj_set_size(p_pill, fw::theme::px(PILL_W), fw::theme::px(PILL_H));
    lv_obj_set_style_radius(p_pill, LV_RADIUS_CIRCLE, 0);
    lv_obj_set_style_bg_opa(p_pill, LV_OPA_COVER, 0);
    lv_obj_set_style_bg_opa(p_pill, LV_OPA_70, LV_STATE_PRESSED);
    lv_obj_set_clickable(p_pill, true);
    lv_obj_set_user_data(p_pill, reinterpret_cast<void*>(static_cast<uintptr_t>(index)));
    lv_obj_add_event_cb(p_pill, &GachStoreApp::on_pill_clicked, LV_EVENT_CLICKED, this);
    lv_obj_t* const p_text = text(p_pill, "", fw::theme::font_small(), fw::palette::LABEL);
    lv_obj_center(p_text);
    if (m_pill_count < MAX_PILLS) {
        m_p_pills[m_pill_count++] = p_pill;
    }
    return p_pill;
}

void GachStoreApp::update_pills()
{
    for (size_t pill = 0U; pill < m_pill_count; ++pill) {
        lv_obj_t* const p_pill = m_p_pills[pill];
        const auto index = static_cast<size_t>(reinterpret_cast<uintptr_t>(lv_obj_get_user_data(p_pill)));
        char label[16];
        uint32_t bg = fw::palette::ACCENT;
        uint32_t fg = fw::palette::LABEL;
        if (index == m_busy_app) {
            size_t received = 0U;
            size_t total = 0U;
            runtime::store_progress(received, total);
            (void)std::snprintf(label, sizeof(label), "%u%%",
                                static_cast<unsigned>((total > 0U) ? ((received * PERCENT) / total) : 0U));
            bg = COLOR_PILL_SECONDARY;
            fg = fw::palette::ACCENT;
        } else {
            switch (action_of(index)) {
            case Action::GET:
                (void)common::copy_string(label, sizeof(label), "GET");
                break;
            case Action::UPDATE:
                (void)common::copy_string(label, sizeof(label), "UPDATE");
                break;
            case Action::OPEN:
                (void)common::copy_string(label, sizeof(label), "OPEN");
                bg = COLOR_PILL_SECONDARY;
                fg = fw::palette::ACCENT;
                break;
            case Action::UNAVAILABLE:
            default:
                (void)common::copy_string(label, sizeof(label), "N/A");
                bg = COLOR_PILL_SECONDARY;
                fg = fw::palette::LABEL_SECONDARY;
                break;
            }
        }
        lv_obj_set_style_bg_color(p_pill, lv_color_hex(bg), 0);
        lv_obj_t* const p_text = lv_obj_get_child(p_pill, 0);
        lv_label_set_text(p_text, label);
        lv_obj_set_style_text_color(p_text, lv_color_hex(fg), 0);
    }
}

lv_obj_t* GachStoreApp::add_app_card(lv_obj_t* p_parent, size_t index)
{
    const runtime::StoreApp& app = runtime::store_app(index);
    lv_obj_t* const p_card = box(p_parent);
    lv_obj_set_size(p_card, LV_PCT(HALF_WIDTH_PCT), LV_SIZE_CONTENT);
    card_style(p_card);
    lv_obj_set_flex_flow(p_card, LV_FLEX_FLOW_ROW);
    lv_obj_set_flex_align(p_card, LV_FLEX_ALIGN_START, LV_FLEX_ALIGN_CENTER, LV_FLEX_ALIGN_CENTER);
    lv_obj_set_style_pad_gap(p_card, fw::theme::px(GAP), 0);
    lv_obj_set_clickable(p_card, true);
    lv_obj_set_user_data(p_card, reinterpret_cast<void*>(static_cast<uintptr_t>(index)));
    lv_obj_add_event_cb(p_card, &GachStoreApp::on_card_clicked, LV_EVENT_CLICKED, this);

    (void)add_icon(p_card, app, ICON_ROW);
    lv_obj_t* const p_info = flex(p_card, LV_FLEX_FLOW_COLUMN, 2);
    lv_obj_set_width(p_info, 0);
    lv_obj_set_flex_grow(p_info, 1);
    lv_obj_t* const p_name = text(p_info, app.name, fw::theme::font_medium(), fw::palette::LABEL);
    lv_label_set_long_mode(p_name, LV_LABEL_LONG_DOT);
    lv_obj_set_width(p_name, LV_PCT(100));
    char size[16];
    fmt_size(app.size, size, sizeof(size));
    char meta[TEXT_LEN];
    (void)std::snprintf(meta, sizeof(meta), "%s  -  %s", (app.category[0] != '\0') ? app.category : "App", size);
    text(p_info, meta, fw::theme::font_small(), fw::palette::LABEL_SECONDARY);
    if (app.description[0] != '\0') {
        lv_obj_t* const p_desc = text(p_info, app.description, fw::theme::font_small(), fw::palette::LABEL_SECONDARY);
        lv_label_set_long_mode(p_desc, LV_LABEL_LONG_DOT);
        lv_obj_set_width(p_desc, LV_PCT(100));
        lv_obj_set_height(p_desc, lv_font_get_line_height(fw::theme::font_small()));
    }
    (void)add_pill(p_card, index);
    return p_card;
}

void GachStoreApp::render_discover()
{
    const size_t count = runtime::store_count();
    if (count == 0U) {
        render_message(LV_SYMBOL_LIST, "The store is empty", true);
        return;
    }
    // ---- Nổi bật: app đầu tiên có ảnh chụp ----
    size_t featured = 0U;
    for (size_t index = 0U; index < count; ++index) {
        if (runtime::store_app(index).shots > 0U) {
            featured = index;
            break;
        }
    }
    const runtime::StoreApp& hero = runtime::store_app(featured);
    add_section_title(m_p_body, "Featured");
    lv_obj_t* const p_hero = box(m_p_body);
    lv_obj_set_size(p_hero, LV_PCT(100), LV_SIZE_CONTENT);
    card_style(p_hero);
    lv_obj_set_flex_flow(p_hero, LV_FLEX_FLOW_ROW);
    lv_obj_set_flex_align(p_hero, LV_FLEX_ALIGN_START, LV_FLEX_ALIGN_CENTER, LV_FLEX_ALIGN_CENTER);
    lv_obj_set_style_pad_gap(p_hero, fw::theme::px(PAGE_PAD), 0);
    lv_obj_set_clickable(p_hero, true);
    lv_obj_set_user_data(p_hero, reinterpret_cast<void*>(static_cast<uintptr_t>(featured)));
    lv_obj_add_event_cb(p_hero, &GachStoreApp::on_card_clicked, LV_EVENT_CLICKED, this);
    if (hero.shots > 0U) {
        m_p_shot_row = p_hero;
        lv_obj_t* const p_slot = box(p_hero);
        lv_obj_set_size(p_slot, fw::theme::px(SHOT_SLOT_W), fw::theme::px(SHOT_SLOT_H));
        lv_obj_set_style_radius(p_slot, fw::theme::px(SHOT_RADIUS), 0);
        lv_obj_set_style_clip_corner(p_slot, true, 0);
        lv_obj_set_style_bg_color(p_slot, lv_color_hex(COLOR_SEPARATOR), 0);
        lv_obj_set_style_bg_opa(p_slot, LV_OPA_COVER, 0);
        m_p_shot_slots[0] = p_slot;
        m_detail = featured; // Ảnh nổi bật dùng chung đường tải ảnh với trang chi tiết.
        m_shot_owner_page = static_cast<uint8_t>(Page::DISCOVER);
    }
    lv_obj_t* const p_text = flex(p_hero, LV_FLEX_FLOW_COLUMN, GAP / 2);
    lv_obj_set_width(p_text, 0);
    lv_obj_set_flex_grow(p_text, 1);
    lv_obj_t* const p_head = flex(p_text, LV_FLEX_FLOW_ROW, GAP);
    lv_obj_set_flex_align(p_head, LV_FLEX_ALIGN_START, LV_FLEX_ALIGN_CENTER, LV_FLEX_ALIGN_CENTER);
    (void)add_icon(p_head, hero, ICON_FEATURED);
    lv_obj_t* const p_titles = flex(p_head, LV_FLEX_FLOW_COLUMN, 2);
    lv_obj_set_width(p_titles, 0);
    lv_obj_set_flex_grow(p_titles, 1);
    text(p_titles, (hero.category[0] != '\0') ? hero.category : "App", fw::theme::font_small(), fw::palette::ACCENT);
    text(p_titles, hero.name, fw::theme::font_large(), fw::palette::LABEL);
    if (hero.description[0] != '\0') {
        lv_obj_t* const p_desc = text(p_text, hero.description, fw::theme::font_small(), fw::palette::LABEL_SECONDARY);
        lv_obj_set_width(p_desc, LV_PCT(100));
    }
    (void)add_pill(p_text, featured);

    // ---- Tất cả app: lưới 2 cột ----
    add_section_title(m_p_body, "All apps");
    lv_obj_t* const p_grid = flex(m_p_body, LV_FLEX_FLOW_ROW_WRAP, GAP);
    lv_obj_set_flex_align(p_grid, LV_FLEX_ALIGN_SPACE_BETWEEN, LV_FLEX_ALIGN_START, LV_FLEX_ALIGN_START);
    for (size_t index = 0U; index < count; ++index) {
        (void)add_app_card(p_grid, index);
    }
    update_pills();
}

void GachStoreApp::render_updates()
{
    const size_t updates = update_count();
    if ((updates == 0U) && (m_busy_app == NONE)) {
        render_message(LV_SYMBOL_OK, "All apps are up to date", false);
        return;
    }
    lv_obj_t* const p_head = flex(m_p_body, LV_FLEX_FLOW_ROW, GAP);
    lv_obj_set_flex_align(p_head, LV_FLEX_ALIGN_SPACE_BETWEEN, LV_FLEX_ALIGN_CENTER, LV_FLEX_ALIGN_CENTER);
    char title[48];
    (void)std::snprintf(title, sizeof(title), "%u update(s) available", static_cast<unsigned>(updates));
    add_section_title(p_head, title);
    lv_obj_t* const p_all = ui::button(p_head, "Update all", &GachStoreApp::on_update_all, this);
    if (m_busy_app != NONE) {
        lv_obj_add_state(p_all, LV_STATE_DISABLED);
    }
    lv_obj_t* const p_grid = flex(m_p_body, LV_FLEX_FLOW_ROW_WRAP, GAP);
    lv_obj_set_flex_align(p_grid, LV_FLEX_ALIGN_SPACE_BETWEEN, LV_FLEX_ALIGN_START, LV_FLEX_ALIGN_START);
    for (size_t index = 0U; index < runtime::store_count(); ++index) {
        if ((action_of(index) == Action::UPDATE) || (index == m_busy_app)) {
            (void)add_app_card(p_grid, index);
        }
    }
    update_pills();
}

void GachStoreApp::render_detail()
{
    const runtime::StoreApp& app = runtime::store_app(m_detail);
    lv_obj_t* const p_back = text(m_p_body, LV_SYMBOL_LEFT "  Store", fw::theme::font_medium(), fw::palette::ACCENT);
    lv_obj_set_clickable(p_back, true);
    lv_obj_add_event_cb(p_back, &GachStoreApp::on_back_clicked, LV_EVENT_CLICKED, this);

    // ---- Đầu: icon, tên, thể loại, nút ----
    lv_obj_t* const p_head = flex(m_p_body, LV_FLEX_FLOW_ROW, PAGE_PAD);
    lv_obj_set_flex_align(p_head, LV_FLEX_ALIGN_START, LV_FLEX_ALIGN_CENTER, LV_FLEX_ALIGN_CENTER);
    (void)add_icon(p_head, app, ICON_DETAIL);
    lv_obj_t* const p_titles = flex(p_head, LV_FLEX_FLOW_COLUMN, GAP / 2);
    lv_obj_set_width(p_titles, 0);
    lv_obj_set_flex_grow(p_titles, 1);
    text(p_titles, app.name, fw::theme::font_large(), fw::palette::LABEL);
    text(p_titles, (app.category[0] != '\0') ? app.category : "App", fw::theme::font_small(),
         fw::palette::LABEL_SECONDARY);
    (void)add_pill(p_titles, m_detail);

    // ---- Thông số ----
    lv_obj_t* const p_stats = flex(m_p_body, LV_FLEX_FLOW_ROW, 0);
    lv_obj_set_style_pad_ver(p_stats, fw::theme::px(GAP), 0);
    lv_obj_set_style_border_side(p_stats, static_cast<lv_border_side_t>(LV_BORDER_SIDE_TOP | LV_BORDER_SIDE_BOTTOM),
                                 0);
    lv_obj_set_style_border_width(p_stats, 1, 0);
    lv_obj_set_style_border_color(p_stats, lv_color_hex(COLOR_SEPARATOR), 0);
    char size[16];
    fmt_size(app.size, size, sizeof(size));
    char sdk[16];
    (void)std::snprintf(sdk, sizeof(sdk), "SDK %u", static_cast<unsigned>(app.sdk));
    const char* const values[][2] = {{"VERSION", app.version}, {"SIZE", size}, {"REQUIRES", sdk},
                                     {"CATEGORY", (app.category[0] != '\0') ? app.category : "App"}};
    for (const auto& item : values) {
        lv_obj_t* const p_cell = flex(p_stats, LV_FLEX_FLOW_COLUMN, 2);
        lv_obj_set_width(p_cell, 0);
        lv_obj_set_flex_grow(p_cell, 1);
        lv_obj_set_flex_align(p_cell, LV_FLEX_ALIGN_START, LV_FLEX_ALIGN_CENTER, LV_FLEX_ALIGN_CENTER);
        text(p_cell, item[0], fw::theme::font_small(), fw::palette::LABEL_SECONDARY);
        text(p_cell, item[1], fw::theme::font_medium(), fw::palette::LABEL);
    }

    // ---- Ảnh chụp (cuộn ngang) ----
    if (app.shots > 0U) {
        add_section_title(m_p_body, "Preview");
        m_p_shot_row = flex(m_p_body, LV_FLEX_FLOW_ROW, GAP);
        lv_obj_set_scrollable(m_p_shot_row, true);
        lv_obj_set_scroll_dir(m_p_shot_row, LV_DIR_HOR);
        for (uint8_t n = 0U; (n < app.shots) && (n < MAX_SHOTS); ++n) {
            lv_obj_t* const p_slot = box(m_p_shot_row);
            lv_obj_set_size(p_slot, fw::theme::px(SHOT_SLOT_W), fw::theme::px(SHOT_SLOT_H));
            lv_obj_set_style_radius(p_slot, fw::theme::px(SHOT_RADIUS), 0);
            lv_obj_set_style_clip_corner(p_slot, true, 0);
            lv_obj_set_style_bg_color(p_slot, lv_color_hex(COLOR_CARD), 0);
            lv_obj_set_style_bg_opa(p_slot, LV_OPA_COVER, 0);
            lv_obj_t* const p_wait = text(p_slot, LV_SYMBOL_IMAGE, fw::theme::font_large(), COLOR_SEPARATOR);
            lv_obj_center(p_wait);
            m_p_shot_slots[n] = p_slot;
        }
        m_shot_owner_page = static_cast<uint8_t>(Page::DETAIL);
    }

    // ---- Mô tả + thông tin ----
    if (app.description[0] != '\0') {
        add_section_title(m_p_body, "About");
        lv_obj_t* const p_desc = text(m_p_body, app.description, fw::theme::font_medium(), fw::palette::LABEL);
        lv_obj_set_width(p_desc, LV_PCT(100));
    }
    add_section_title(m_p_body, "Information");
    lv_obj_t* const p_info = box(m_p_body);
    lv_obj_set_size(p_info, LV_PCT(100), LV_SIZE_CONTENT);
    card_style(p_info);
    lv_obj_set_flex_flow(p_info, LV_FLEX_FLOW_COLUMN);
    lv_obj_set_style_pad_gap(p_info, fw::theme::px(GAP / 2), 0);
    runtime::PackageDetails installed{};
    const bool has = runtime::package_details(app.id, installed);
    const char* const rows[][2] = {{"Identifier", app.id},
                                   {"Installed", has ? installed.p_version : "Not installed"},
                                   {"Store", runtime::store_url()}};
    for (const auto& row : rows) {
        lv_obj_t* const p_row = flex(p_info, LV_FLEX_FLOW_ROW, GAP);
        lv_obj_set_flex_align(p_row, LV_FLEX_ALIGN_SPACE_BETWEEN, LV_FLEX_ALIGN_CENTER, LV_FLEX_ALIGN_CENTER);
        text(p_row, row[0], fw::theme::font_small(), fw::palette::LABEL_SECONDARY);
        text(p_row, row[1], fw::theme::font_small(), fw::palette::LABEL);
    }
    update_pills();
}

// ============================================================================
// Ảnh chụp
// ============================================================================
bool GachStoreApp::show_shot(uint8_t n)
{
    lv_obj_t* const p_slot = m_p_shot_slots[n - 1U];
    runtime::StoreImage image{};
    if ((p_slot == nullptr) || !runtime::store_read_shot(m_detail, n, s_shot_buf[n - 1U], SHOT_BUF_BYTES, image)) {
        return false;
    }
    lv_image_dsc_t& dsc = s_shot_dsc[n - 1U];
    dsc = lv_image_dsc_t{};
    dsc.header.magic = LV_IMAGE_HEADER_MAGIC;
    dsc.header.cf = image.has_alpha ? LV_COLOR_FORMAT_RGB565A8 : LV_COLOR_FORMAT_RGB565;
    dsc.header.w = image.width;
    dsc.header.h = image.height;
    dsc.header.stride = image.stride;
    dsc.data_size = image.data_size;
    dsc.data = image.p_data;
    lv_obj_clean(p_slot);
    lv_obj_t* const p_img = lv_image_create(p_slot);
    lv_image_set_src(p_img, &dsc);
    lv_obj_set_size(p_img, LV_PCT(100), LV_PCT(100));
    lv_image_set_inner_align(p_img, LV_IMAGE_ALIGN_COVER);
    return true;
}

void GachStoreApp::load_shots()
{
    if ((m_p_shot_row == nullptr) || (m_shot_pending != 0U)) {
        return;
    }
    const runtime::StoreApp& app = runtime::store_app(m_detail);
    for (uint8_t n = 1U; (n <= app.shots) && (n <= MAX_SHOTS); ++n) {
        lv_obj_t* const p_slot = m_p_shot_slots[n - 1U];
        if ((p_slot == nullptr) || ((lv_obj_get_child_count(p_slot) > 0U) &&
                                    lv_obj_check_type(lv_obj_get_child(p_slot, 0), &lv_image_class))) {
            continue; // Không có ô / đã hiện ảnh.
        }
        if (runtime::store_shot_cached(m_detail, n)) {
            (void)show_shot(n);
            continue;
        }
        if (!is_busy(m_last_state) && common::is_ok(runtime::store_fetch_shot(m_detail, n))) {
            m_shot_pending = n;
            m_last_state = runtime::StoreState::FETCHING_SHOT;
        }
        return; // Một ảnh mỗi lần (một kênh HTTP).
    }
}

// ============================================================================
// Sự kiện
// ============================================================================
void GachStoreApp::show_detail(size_t index)
{
    if (m_page != Page::DETAIL) {
        m_return_page = m_page;
    }
    m_detail = index;
    m_page = Page::DETAIL;
    render();
}

void GachStoreApp::on_tab_discover(lv_event_t* p_event)
{
    auto* const p_self = static_cast<GachStoreApp*>(lv_event_get_user_data(p_event));
    p_self->m_page = Page::DISCOVER;
    p_self->render();
}

void GachStoreApp::on_tab_updates(lv_event_t* p_event)
{
    auto* const p_self = static_cast<GachStoreApp*>(lv_event_get_user_data(p_event));
    p_self->m_page = Page::UPDATES;
    p_self->render();
}

void GachStoreApp::on_refresh(lv_event_t* p_event)
{
    auto* const p_self = static_cast<GachStoreApp*>(lv_event_get_user_data(p_event));
    if (common::is_ok(runtime::store_refresh())) {
        p_self->m_last_state = runtime::StoreState::LOADING_INDEX;
        p_self->m_shot_pending = 0U;
        p_self->set_status("", fw::palette::LABEL_SECONDARY);
        p_self->render();
    } else {
        p_self->set_status("Busy, try again in a moment", fw::palette::DESTRUCTIVE);
    }
}

void GachStoreApp::on_card_clicked(lv_event_t* p_event)
{
    auto* const p_self = static_cast<GachStoreApp*>(lv_event_get_user_data(p_event));
    void* const p_data = lv_obj_get_user_data(lv_event_get_current_target_obj(p_event));
    p_self->show_detail(static_cast<size_t>(reinterpret_cast<uintptr_t>(p_data)));
}

void GachStoreApp::on_pill_clicked(lv_event_t* p_event)
{
    auto* const p_self = static_cast<GachStoreApp*>(lv_event_get_user_data(p_event));
    void* const p_data = lv_obj_get_user_data(lv_event_get_current_target_obj(p_event));
    p_self->act(static_cast<size_t>(reinterpret_cast<uintptr_t>(p_data)));
}

void GachStoreApp::on_back_clicked(lv_event_t* p_event)
{
    (void)static_cast<GachStoreApp*>(lv_event_get_user_data(p_event))->on_back();
}

void GachStoreApp::on_update_all(lv_event_t* p_event)
{
    auto* const p_self = static_cast<GachStoreApp*>(lv_event_get_user_data(p_event));
    p_self->m_update_all = true;
    p_self->start_next_update();
}

void GachStoreApp::on_timer(lv_timer_t* p_timer)
{
    auto* const p_self = static_cast<GachStoreApp*>(lv_timer_get_user_data(p_timer));
    const runtime::StoreState state = runtime::store_poll();
    const runtime::StoreState previous = p_self->m_last_state;
    if (!p_self->m_loaded && (state == runtime::StoreState::FAILED) && (++p_self->m_retry_ticks >= RETRY_TICKS)) {
        p_self->m_retry_ticks = 0U;
        if (common::is_ok(runtime::store_refresh())) {
            p_self->m_last_state = runtime::StoreState::LOADING_INDEX; // Giữ màn báo lỗi tới khi có kết quả.
        }
        return;
    }
    if (state == previous) {
        if (state == runtime::StoreState::DOWNLOADING) {
            p_self->update_pills(); // Tiến độ %.
        }
        return;
    }
    p_self->m_last_state = state;
    if (previous == runtime::StoreState::LOADING_INDEX) {
        p_self->m_loaded = (state == runtime::StoreState::DONE);
        p_self->render();
    } else if (previous == runtime::StoreState::DOWNLOADING) {
        const bool ok = (state == runtime::StoreState::DONE);
        p_self->m_busy_app = NONE;
        p_self->set_status(runtime::store_message(), ok ? fw::palette::SUCCESS : fw::palette::DESTRUCTIVE);
        if (ok && p_self->m_update_all) {
            p_self->start_next_update();
        } else {
            p_self->m_update_all = false;
        }
        p_self->update_header();
        p_self->update_pills();
        if (p_self->m_page == Page::UPDATES) {
            p_self->render();
        }
    } else if (previous == runtime::StoreState::FETCHING_SHOT) {
        const uint8_t n = p_self->m_shot_pending;
        p_self->m_shot_pending = 0U;
        if ((state == runtime::StoreState::DONE) && (n != 0U)) {
            (void)p_self->show_shot(n);
        }
        if (state == runtime::StoreState::DONE) {
            p_self->load_shots(); // Ảnh kế tiếp.
        }
    } else {
        // Chuyển trạng thái khác: không cần làm gì.
    }
}

} // namespace apps
