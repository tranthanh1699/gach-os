/**
 * @file    files_app.cpp
 * @brief   Hiện thực File Manager.
 */
#include "files_app.hpp"

#include <cstdio>
#include <cstring>

#include "common/log.hpp"
#include "common/string_utils.hpp"
#include "framework/services.hpp"
#include "framework/system_ui.hpp"
#include "framework/theme.hpp"
#include "runtime/runtime.hpp"

namespace apps {

namespace {

constexpr const char* TAG = "files";
constexpr fw::AppInfo INFO{"files", "Files", LV_SYMBOL_DIRECTORY, 0xF0A04BU, true, false};

// ---- Thư mục đặc biệt ----
constexpr const char* DIR_APPS = "apps";
constexpr const char* DIR_INSTALL = "install";
constexpr const char* DIR_SYSTEM = ".gach";
constexpr const char* DIR_TRASH = ".gach/trash";
constexpr const char* DIR_TRASH_INFO = ".gach/trashinfo";
constexpr const char* FILE_RECENT = ".gach/recent";

// ---- Màu (dark, giống Files của GNOME) ----
constexpr uint32_t COLOR_MAIN_BG = 0x1E1E1EU;
constexpr uint32_t COLOR_SIDE_BG = 0x2A2A2AU;
constexpr uint32_t COLOR_SIDE_SELECTED = 0x3D3D3DU;
constexpr uint32_t COLOR_PILL = 0x323232U;
constexpr uint32_t COLOR_ROW_PRESSED = 0x2C2C2EU;
constexpr uint32_t COLOR_TILE = 0x3A3A3CU;
constexpr uint32_t COLOR_TEXT = 0xFFFFFFU;
constexpr uint32_t COLOR_DIM = 0x9A9A9AU;
constexpr uint32_t COLOR_FOLDER = 0xF0A04BU;
constexpr uint32_t COLOR_GAPP = 0x0A84FFU;
constexpr uint32_t COLOR_IMAGE = 0x30D158U;
constexpr uint32_t COLOR_AUDIO = 0xFF375FU;
constexpr uint32_t COLOR_VIDEO = 0xBF5AF2U;
constexpr uint32_t COLOR_TEXTFILE = 0xC7C7CCU;
constexpr uint32_t COLOR_CODE = 0xFFD60AU;
constexpr uint32_t COLOR_OTHER = 0x8E8E93U;
constexpr lv_opa_t SELECTED_OPA = 90;

// ---- Kích thước (dp) ----
constexpr int32_t SIDEBAR_W = 190;
constexpr int32_t PAD = 10;
constexpr int32_t GAP = 8;
constexpr int32_t RADIUS = 10;
constexpr int32_t SIDE_ITEM_PAD_HOR = 12;
constexpr int32_t SIDE_ITEM_PAD_VER = 9;
constexpr int32_t ICON_BTN = 36;
constexpr int32_t ROW_H = 46;
constexpr int32_t ROW_ICON = 32;
constexpr int32_t COL_SIZE_W = 110;
constexpr int32_t COL_DATE_W = 150;
constexpr int32_t TILE_W = 112;
constexpr int32_t TILE_ICON = 56;
constexpr int32_t TILE_NAME_H = 36;
constexpr int32_t STORAGE_BAR_H = 6;
constexpr int32_t MENU_W = 230;
constexpr int32_t PROMPT_W = 380;
constexpr int32_t PROMPT_TOP = 30;
constexpr int32_t ACTION_H = 34;
constexpr uint32_t JOB_PERIOD_MS = 20U;
constexpr uint32_t JOB_BUDGET_MS = 12U;
constexpr int MAX_UNIQUE_SUFFIX = 99;

// ---- Thời gian ----
constexpr uint32_t SECONDS_PER_MINUTE = 60U;
constexpr uint32_t SECONDS_PER_DAY = 86400U;
constexpr uint32_t MIN_VALID_EPOCH = 1577836800U; /**< 2020-01-01: FAT chưa có giờ thật thì hiện "-". */

// ---- Kích thước byte ----
constexpr uint32_t KIB = 1024U;
constexpr uint32_t MIB = KIB * KIB;
constexpr uint64_t GIB = static_cast<uint64_t>(MIB) * KIB;

const char* const SIDE_NAMES[] = {"Recent", "Home", "Apps", "Install", "Trash"};
const char* const SIDE_SYMBOLS[] = {LV_SYMBOL_REFRESH, LV_SYMBOL_HOME, LV_SYMBOL_LIST, LV_SYMBOL_DOWNLOAD,
                                    LV_SYMBOL_TRASH};

// ---- Nút hành động / mục menu (user_data) ----
enum Action : uintptr_t {
    ACT_COPY = 1U,
    ACT_CUT,
    ACT_RENAME,
    ACT_DELETE,
    ACT_RESTORE,
    ACT_FOREVER,
    ACT_SELECT_ALL,
    ACT_CANCEL_SEL,
    ACT_CANCEL_JOB,
    MENU_NEW_FOLDER,
    MENU_HIDDEN,
    MENU_REFRESH,
    MENU_EMPTY_TRASH
};

// ---------------------------------------------------------------------------
// Tiện ích chung
// ---------------------------------------------------------------------------
lv_obj_t* plain(lv_obj_t* p_parent)
{
    lv_obj_t* const p_obj = lv_obj_create(p_parent);
    lv_obj_remove_style_all(p_obj);
    lv_obj_set_scrollable(p_obj, false);
    lv_obj_set_clickable(p_obj, false);
    return p_obj;
}

lv_obj_t* flex(lv_obj_t* p_parent, lv_flex_flow_t flow, int32_t gap_dp)
{
    lv_obj_t* const p_obj = plain(p_parent);
    lv_obj_set_flex_flow(p_obj, flow);
    lv_obj_set_flex_align(p_obj, LV_FLEX_ALIGN_START, LV_FLEX_ALIGN_CENTER, LV_FLEX_ALIGN_CENTER);
    lv_obj_set_style_pad_gap(p_obj, fw::theme::px(gap_dp), 0);
    return p_obj;
}

lv_obj_t* text(lv_obj_t* p_parent, const char* p_text, uint32_t color, const lv_font_t* p_font)
{
    lv_obj_t* const p_label = lv_label_create(p_parent);
    lv_label_set_text(p_label, p_text);
    lv_obj_set_style_text_color(p_label, lv_color_hex(color), 0);
    lv_obj_set_style_text_font(p_label, p_font, 0);
    return p_label;
}

/** @brief Nút phẳng (chữ / ký hiệu), nhấn sáng lên. */
lv_obj_t* flat_button(lv_obj_t* p_parent, const char* p_text, lv_event_cb_t cb, void* p_user, uintptr_t tag)
{
    lv_obj_t* const p_btn = lv_button_create(p_parent);
    lv_obj_remove_style_all(p_btn);
    lv_obj_set_size(p_btn, LV_SIZE_CONTENT, fw::theme::px(ICON_BTN));
    lv_obj_set_style_min_width(p_btn, fw::theme::px(ICON_BTN), 0);
    lv_obj_set_style_pad_hor(p_btn, fw::theme::px(PAD), 0);
    lv_obj_set_style_radius(p_btn, fw::theme::px(RADIUS), 0);
    lv_obj_set_style_bg_color(p_btn, lv_color_hex(COLOR_PILL), 0);
    lv_obj_set_style_bg_opa(p_btn, LV_OPA_COVER, 0);
    lv_obj_set_style_bg_color(p_btn, lv_color_hex(COLOR_SIDE_SELECTED), LV_STATE_PRESSED);
    lv_obj_set_style_opa(p_btn, LV_OPA_40, LV_STATE_DISABLED);
    lv_obj_set_style_text_color(p_btn, lv_color_hex(COLOR_TEXT), 0);
    lv_obj_set_user_data(p_btn, reinterpret_cast<void*>(tag));
    lv_obj_add_event_cb(p_btn, cb, LV_EVENT_CLICKED, p_user);
    lv_obj_t* const p_label = lv_label_create(p_btn);
    lv_label_set_text(p_label, p_text);
    lv_obj_center(p_label);
    return p_btn;
}

void set_enabled(lv_obj_t* p_obj, bool is_enabled)
{
    if (is_enabled) {
        lv_obj_remove_state(p_obj, LV_STATE_DISABLED);
    } else {
        lv_obj_add_state(p_obj, LV_STATE_DISABLED);
    }
}

char lower(char c)
{
    return ((c >= 'A') && (c <= 'Z')) ? static_cast<char>(c - 'A' + 'a') : c;
}

int compare_nocase(const char* p_a, const char* p_b)
{
    for (;; ++p_a, ++p_b) {
        const char a = lower(*p_a);
        const char b = lower(*p_b);
        if ((a != b) || (a == '\0')) {
            return static_cast<int>(static_cast<unsigned char>(a)) - static_cast<int>(static_cast<unsigned char>(b));
        }
    }
}

bool has_ext(const char* p_name, const char* const* p_exts)
{
    const char* const p_dot = std::strrchr(p_name, '.');
    if (p_dot == nullptr) {
        return false;
    }
    for (const char* const* p_ext = p_exts; *p_ext != nullptr; ++p_ext) {
        if (compare_nocase(p_dot + 1, *p_ext) == 0) {
            return true;
        }
    }
    return false;
}

const char* const EXT_GAPP[] = {"gapp", nullptr};
const char* const EXT_IMAGE[] = {"png", "jpg", "jpeg", "bmp", "gif", nullptr};
const char* const EXT_AUDIO[] = {"mp3", "wav", "ogg", "flac", nullptr};
const char* const EXT_VIDEO[] = {"mp4", "avi", "mkv", "mov", nullptr};
const char* const EXT_TEXT[] = {"txt", "md", "log", "csv", nullptr};
const char* const EXT_CODE[] = {"lua", "json", "ini", "cfg", "xml", "yml", "yaml", "html", "js", nullptr};

void icon_for(const fw::FileEntry& entry, const char*& p_symbol, uint32_t& color)
{
    if (entry.is_dir) {
        p_symbol = LV_SYMBOL_DIRECTORY;
        color = COLOR_FOLDER;
    } else if (has_ext(entry.name, EXT_GAPP)) {
        p_symbol = LV_SYMBOL_DOWNLOAD;
        color = COLOR_GAPP;
    } else if (has_ext(entry.name, EXT_IMAGE)) {
        p_symbol = LV_SYMBOL_IMAGE;
        color = COLOR_IMAGE;
    } else if (has_ext(entry.name, EXT_AUDIO)) {
        p_symbol = LV_SYMBOL_AUDIO;
        color = COLOR_AUDIO;
    } else if (has_ext(entry.name, EXT_VIDEO)) {
        p_symbol = LV_SYMBOL_VIDEO;
        color = COLOR_VIDEO;
    } else if (has_ext(entry.name, EXT_TEXT)) {
        p_symbol = LV_SYMBOL_FILE;
        color = COLOR_TEXTFILE;
    } else if (has_ext(entry.name, EXT_CODE)) {
        p_symbol = LV_SYMBOL_EDIT;
        color = COLOR_CODE;
    } else {
        p_symbol = LV_SYMBOL_FILE;
        color = COLOR_OTHER;
    }
}

bool is_text_file(const char* p_name)
{
    return has_ext(p_name, EXT_TEXT) || has_ext(p_name, EXT_CODE) || (std::strchr(p_name, '.') == nullptr);
}

void format_size(uint64_t bytes, char* p_out, size_t len)
{
    if (bytes >= GIB) {
        (void)std::snprintf(p_out, len, "%.1f GB", static_cast<double>(bytes) / static_cast<double>(GIB));
    } else if (bytes >= MIB) {
        (void)std::snprintf(p_out, len, "%.1f MB", static_cast<double>(bytes) / static_cast<double>(MIB));
    } else if (bytes >= KIB) {
        (void)std::snprintf(p_out, len, "%.1f kB", static_cast<double>(bytes) / static_cast<double>(KIB));
    } else {
        (void)std::snprintf(p_out, len, "%u bytes", static_cast<unsigned>(bytes));
    }
}

/** @brief Giây UTC -> "dd/mm/yyyy hh:mm" theo múi giờ của máy (thuật toán days-from-civil ngược). */
void format_time(uint32_t mtime, int32_t utc_offset_min, char* p_out, size_t len)
{
    if (mtime < MIN_VALID_EPOCH) {
        (void)std::snprintf(p_out, len, "-");
        return;
    }
    const int64_t local = static_cast<int64_t>(mtime) + (static_cast<int64_t>(utc_offset_min) * SECONDS_PER_MINUTE);
    const int64_t days = local / SECONDS_PER_DAY;
    const int64_t secs = local % SECONDS_PER_DAY;
    // Howard Hinnant, civil_from_days.
    constexpr int64_t DAYS_TO_0000 = 719468;
    constexpr int64_t DAYS_PER_ERA = 146097;
    const int64_t z = days + DAYS_TO_0000;
    const int64_t era = z / DAYS_PER_ERA;
    const int64_t doe = z - (era * DAYS_PER_ERA);
    const int64_t yoe = (doe - doe / 1460 + doe / 36524 - doe / 146096) / 365;
    const int64_t doy = doe - (365 * yoe + yoe / 4 - yoe / 100);
    const int64_t mp = (5 * doy + 2) / 153;
    const int64_t day = doy - (153 * mp + 2) / 5 + 1;
    const int64_t month = (mp < 10) ? (mp + 3) : (mp - 9);
    const int64_t year = yoe + era * 400 + ((month <= 2) ? 1 : 0);
    (void)std::snprintf(p_out, len, "%02d/%02d/%04d %02d:%02d", static_cast<int>(day), static_cast<int>(month),
                        static_cast<int>(year), static_cast<int>(secs / 3600), static_cast<int>((secs / 60) % 60));
}

struct CollectCtx {
    fw::FileEntry* p_entries;
    size_t count;
    size_t capacity;
    bool show_hidden;
    bool truncated;
};

bool collect(const fw::FileEntry& entry, void* p_ctx)
{
    auto* const p_collect = static_cast<CollectCtx*>(p_ctx);
    if (!p_collect->show_hidden && (entry.name[0] == '.')) {
        return true;
    }
    if (p_collect->count >= p_collect->capacity) {
        p_collect->truncated = true;
        return false;
    }
    p_collect->p_entries[p_collect->count++] = entry;
    return true;
}

bool count_child(const fw::FileEntry& entry, void* p_ctx)
{
    if (entry.name[0] != '.') {
        ++*static_cast<size_t*>(p_ctx);
    }
    return true;
}

/** @brief Tên trong thùng rác có dạng "<n>-<tên gốc>": bỏ tiền tố số. */
const char* strip_trash_prefix(const char* p_name)
{
    const char* p = p_name;
    while ((*p >= '0') && (*p <= '9')) {
        ++p;
    }
    return ((p != p_name) && (*p == '-')) ? (p + 1) : p_name;
}

} // namespace

// ============================================================================
// Vòng đời
// ============================================================================
FilesApp::FilesApp()
    : fw::App(INFO), m_p_root(nullptr), m_p_side_items{}, m_p_storage_bar(nullptr), m_p_storage_label(nullptr),
      m_p_back_btn(nullptr), m_p_fwd_btn(nullptr), m_p_crumbs(nullptr), m_p_paste_btn(nullptr), m_p_view_btn(nullptr),
      m_p_header(nullptr), m_p_header_cells{}, m_p_list(nullptr), m_p_footer_label(nullptr), m_p_actions(nullptr),
      m_p_pager(nullptr), m_p_page_label(nullptr), m_p_menu(nullptr), m_p_prompt(nullptr), m_p_prompt_field(nullptr),
      m_p_viewer(nullptr), m_p_dialog(nullptr), m_p_job_timer(nullptr), m_p_items{}, m_location(Location::HOME),
      m_path{}, m_entries{}, m_order{}, m_selected{}, m_count(0U), m_selected_count(0U), m_page(0U),
      m_truncated(false), m_is_grid(false), m_show_hidden(false), m_sort(SortKey::NAME), m_sort_desc(false),
      m_p_long_pressed(nullptr), m_back{}, m_back_count(0U), m_forward{}, m_forward_count(0U), m_clip{},
      m_clip_count(0U), m_clip_op(ClipOp::NONE), m_queue{}, m_queue_count(0U), m_queue_next(0U),
      m_queue_kind(QueueKind::NONE), m_queue_dir{}, m_queue_done(0U), m_queue_failed(0U), m_recent{},
      m_recent_count(0U), m_prompt_kind(PromptKind::NONE), m_target{}, m_text{}, m_status{}
{
}

void FilesApp::on_create(lv_obj_t* p_root)
{
    reset_ui(); // Object app sống suốt đời máy, giao diện thì không: bỏ mọi con trỏ của lần mở trước.
    m_p_root = p_root;
    lv_obj_set_style_bg_color(p_root, lv_color_hex(COLOR_MAIN_BG), 0);
    lv_obj_set_style_bg_opa(p_root, LV_OPA_COVER, 0);
    lv_obj_set_flex_flow(p_root, LV_FLEX_FLOW_ROW);
    lv_obj_set_scrollable(p_root, false);
    build_sidebar(p_root);
    build_main(p_root);
    load_recent();
    open_location(Location::HOME);
}

void FilesApp::on_resume()
{
    render_storage();
    if (m_p_list != nullptr) {
        reload(); // Thẻ có thể đã đổi (cài app, gach push...).
    }
}

void FilesApp::on_pause()
{
    close_menu();
    close_dialog();
    close_prompt();
}

void FilesApp::on_destroy()
{
    close_dialog(); // Hộp thoại nằm trên top layer: không bị xóa cùng root.
    reset_ui();
}

void FilesApp::reset_ui()
{
    if (m_p_job_timer != nullptr) {
        lv_timer_delete(m_p_job_timer);
        m_p_job_timer = nullptr;
    }
    if (m_queue_kind != QueueKind::NONE) {
        context().services.files().cancel_job();
        m_queue_kind = QueueKind::NONE;
    }
    // Widget thuộc root của lần mở trước (đã / sắp bị xóa): chỉ quên con trỏ, không chạm vào.
    m_p_root = nullptr;
    for (lv_obj_t*& p_item : m_p_side_items) {
        p_item = nullptr;
    }
    for (lv_obj_t*& p_cell : m_p_header_cells) {
        p_cell = nullptr;
    }
    for (lv_obj_t*& p_item : m_p_items) {
        p_item = nullptr;
    }
    m_p_storage_bar = nullptr;
    m_p_storage_label = nullptr;
    m_p_back_btn = nullptr;
    m_p_fwd_btn = nullptr;
    m_p_crumbs = nullptr;
    m_p_paste_btn = nullptr;
    m_p_view_btn = nullptr;
    m_p_header = nullptr;
    m_p_list = nullptr;
    m_p_footer_label = nullptr;
    m_p_actions = nullptr;
    m_p_pager = nullptr;
    m_p_page_label = nullptr;
    m_p_menu = nullptr;
    m_p_prompt = nullptr;
    m_p_prompt_field = nullptr;
    m_p_viewer = nullptr;
    m_p_dialog = nullptr;
    m_p_long_pressed = nullptr;
    m_prompt_kind = PromptKind::NONE;
    std::memset(m_selected, 0, sizeof(m_selected));
    m_selected_count = 0U;
}

bool FilesApp::on_back()
{
    if (m_p_dialog != nullptr) {
        close_dialog();
        return true;
    }
    if (m_p_menu != nullptr) {
        close_menu();
        return true;
    }
    if (m_p_prompt != nullptr) {
        close_prompt();
        return true;
    }
    if (m_p_viewer != nullptr) {
        close_viewer();
        return true;
    }
    if (m_selected_count > 0U) {
        clear_selection();
        return true;
    }
    if (m_back_count > 0U) {
        history_step(false); // Như nút ‹.
        return true;
    }
    return false;
}

// ============================================================================
// Dựng giao diện
// ============================================================================
void FilesApp::build_sidebar(lv_obj_t* p_parent)
{
    lv_obj_t* const p_side = flex(p_parent, LV_FLEX_FLOW_COLUMN, 2);
    lv_obj_set_size(p_side, fw::theme::px(SIDEBAR_W), LV_PCT(100));
    lv_obj_set_style_bg_color(p_side, lv_color_hex(COLOR_SIDE_BG), 0);
    lv_obj_set_style_bg_opa(p_side, LV_OPA_COVER, 0);
    lv_obj_set_style_pad_all(p_side, fw::theme::px(PAD), 0);
    lv_obj_set_flex_align(p_side, LV_FLEX_ALIGN_START, LV_FLEX_ALIGN_START, LV_FLEX_ALIGN_START);

    lv_obj_t* const p_title = text(p_side, "Files", COLOR_TEXT, fw::theme::font_medium());
    lv_obj_set_width(p_title, LV_PCT(100));
    lv_obj_set_style_text_align(p_title, LV_TEXT_ALIGN_CENTER, 0);
    lv_obj_set_style_pad_bottom(p_title, fw::theme::px(GAP), 0);

    for (size_t index = 0U; index < static_cast<size_t>(Location::COUNT); ++index) {
        lv_obj_t* const p_item = flex(p_side, LV_FLEX_FLOW_ROW, PAD);
        lv_obj_set_width(p_item, LV_PCT(100));
        lv_obj_set_style_pad_hor(p_item, fw::theme::px(SIDE_ITEM_PAD_HOR), 0);
        lv_obj_set_style_pad_ver(p_item, fw::theme::px(SIDE_ITEM_PAD_VER), 0);
        lv_obj_set_style_radius(p_item, fw::theme::px(RADIUS), 0);
        lv_obj_set_style_bg_color(p_item, lv_color_hex(COLOR_SIDE_SELECTED), LV_STATE_CHECKED);
        lv_obj_set_style_bg_opa(p_item, LV_OPA_COVER, LV_STATE_CHECKED);
        lv_obj_set_style_bg_color(p_item, lv_color_hex(COLOR_ROW_PRESSED), LV_STATE_PRESSED);
        lv_obj_set_style_bg_opa(p_item, LV_OPA_COVER, LV_STATE_PRESSED);
        lv_obj_set_clickable(p_item, true);
        lv_obj_set_user_data(p_item, reinterpret_cast<void*>(index));
        lv_obj_add_event_cb(p_item, &FilesApp::on_sidebar, LV_EVENT_CLICKED, this);
        (void)text(p_item, SIDE_SYMBOLS[index], COLOR_DIM, fw::theme::font_small());
        (void)text(p_item, SIDE_NAMES[index], COLOR_TEXT, fw::theme::font_small());
        m_p_side_items[index] = p_item;
    }

    lv_obj_t* const p_spacer = plain(p_side);
    lv_obj_set_flex_grow(p_spacer, 1);
    lv_obj_set_width(p_spacer, LV_PCT(100));

    (void)text(p_side, LV_SYMBOL_SD_CARD "  SD card", COLOR_DIM, fw::theme::font_small());
    m_p_storage_bar = lv_bar_create(p_side);
    lv_obj_remove_style_all(m_p_storage_bar);
    lv_obj_set_size(m_p_storage_bar, LV_PCT(100), fw::theme::px(STORAGE_BAR_H));
    lv_obj_set_style_bg_color(m_p_storage_bar, lv_color_hex(COLOR_TILE), LV_PART_MAIN);
    lv_obj_set_style_bg_opa(m_p_storage_bar, LV_OPA_COVER, LV_PART_MAIN);
    lv_obj_set_style_radius(m_p_storage_bar, LV_RADIUS_CIRCLE, LV_PART_MAIN);
    lv_obj_set_style_bg_color(m_p_storage_bar, lv_color_hex(fw::palette::ACCENT), LV_PART_INDICATOR);
    lv_obj_set_style_bg_opa(m_p_storage_bar, LV_OPA_COVER, LV_PART_INDICATOR);
    lv_obj_set_style_radius(m_p_storage_bar, LV_RADIUS_CIRCLE, LV_PART_INDICATOR);
    m_p_storage_label = text(p_side, "", COLOR_DIM, fw::theme::font_small());
}

void FilesApp::build_main(lv_obj_t* p_parent)
{
    lv_obj_t* const p_main = flex(p_parent, LV_FLEX_FLOW_COLUMN, GAP);
    lv_obj_set_height(p_main, LV_PCT(100));
    lv_obj_set_flex_grow(p_main, 1);
    lv_obj_set_style_pad_all(p_main, fw::theme::px(PAD), 0);
    lv_obj_set_flex_align(p_main, LV_FLEX_ALIGN_START, LV_FLEX_ALIGN_START, LV_FLEX_ALIGN_START);
    build_toolbar(p_main);
    build_header(p_main);

    m_p_list = lv_obj_create(p_main);
    lv_obj_remove_style_all(m_p_list);
    lv_obj_set_width(m_p_list, LV_PCT(100));
    lv_obj_set_flex_grow(m_p_list, 1);
    lv_obj_set_scroll_dir(m_p_list, LV_DIR_VER);
    lv_obj_set_clickable(m_p_list, true); // Kéo trên khoảng trống cũng cuộn được.
    lv_obj_set_style_pad_gap(m_p_list, fw::theme::px(2), 0);

    build_footer(p_main);
}

void FilesApp::build_toolbar(lv_obj_t* p_parent)
{
    lv_obj_t* const p_bar = flex(p_parent, LV_FLEX_FLOW_ROW, GAP);
    lv_obj_set_width(p_bar, LV_PCT(100));
    m_p_back_btn = flat_button(p_bar, LV_SYMBOL_LEFT, &FilesApp::on_history, this, 0U);
    m_p_fwd_btn = flat_button(p_bar, LV_SYMBOL_RIGHT, &FilesApp::on_history, this, 1U);

    m_p_crumbs = flex(p_bar, LV_FLEX_FLOW_ROW, 2);
    lv_obj_set_height(m_p_crumbs, fw::theme::px(ICON_BTN));
    lv_obj_set_flex_grow(m_p_crumbs, 1);
    lv_obj_set_style_bg_color(m_p_crumbs, lv_color_hex(COLOR_PILL), 0);
    lv_obj_set_style_bg_opa(m_p_crumbs, LV_OPA_COVER, 0);
    lv_obj_set_style_radius(m_p_crumbs, fw::theme::px(RADIUS), 0);
    lv_obj_set_style_pad_hor(m_p_crumbs, fw::theme::px(GAP), 0);
    lv_obj_set_style_clip_corner(m_p_crumbs, true, 0);

    m_p_paste_btn = flat_button(p_bar, LV_SYMBOL_PASTE " Paste", &FilesApp::on_paste, this, 0U);
    lv_obj_set_style_bg_color(m_p_paste_btn, lv_color_hex(fw::palette::ACCENT), 0);
    m_p_view_btn = flat_button(p_bar, LV_SYMBOL_IMAGE, &FilesApp::on_view_toggle, this, 0U);
    (void)flat_button(p_bar, LV_SYMBOL_BARS, &FilesApp::on_menu, this, 0U);
}

void FilesApp::build_header(lv_obj_t* p_parent)
{
    m_p_header = flex(p_parent, LV_FLEX_FLOW_ROW, 12);
    lv_obj_set_width(m_p_header, LV_PCT(100));
    lv_obj_set_style_pad_hor(m_p_header, fw::theme::px(PAD), 0);
    static const char* const TITLES[] = {"Name", "Size", "Modified"};
    const int32_t widths[] = {0, fw::theme::px(COL_SIZE_W), fw::theme::px(COL_DATE_W)};
    // Ô "Name" lùi bằng icon để thẳng cột chữ của các dòng.
    lv_obj_t* const p_indent = plain(m_p_header);
    lv_obj_set_size(p_indent, fw::theme::px(ROW_ICON), 1);
    for (size_t index = 0U; index < 3U; ++index) {
        lv_obj_t* const p_cell = text(m_p_header, TITLES[index], COLOR_DIM, fw::theme::font_small());
        if (index == 0U) {
            lv_obj_set_flex_grow(p_cell, 1);
        } else {
            lv_obj_set_width(p_cell, widths[index]);
        }
        lv_obj_set_clickable(p_cell, true);
        lv_obj_set_user_data(p_cell, reinterpret_cast<void*>(index));
        lv_obj_add_event_cb(p_cell, &FilesApp::on_header, LV_EVENT_CLICKED, this);
        m_p_header_cells[index] = p_cell;
    }
}

void FilesApp::build_footer(lv_obj_t* p_parent)
{
    lv_obj_t* const p_footer = flex(p_parent, LV_FLEX_FLOW_ROW, GAP);
    lv_obj_set_width(p_footer, LV_PCT(100));
    lv_obj_set_height(p_footer, fw::theme::px(ICON_BTN));
    m_p_footer_label = text(p_footer, "", COLOR_DIM, fw::theme::font_small());
    lv_obj_set_flex_grow(m_p_footer_label, 1);
    lv_label_set_long_mode(m_p_footer_label, LV_LABEL_LONG_DOT);
    m_p_actions = flex(p_footer, LV_FLEX_FLOW_ROW, GAP);
    lv_obj_set_size(m_p_actions, LV_SIZE_CONTENT, LV_SIZE_CONTENT);
    m_p_pager = flex(p_footer, LV_FLEX_FLOW_ROW, GAP);
    lv_obj_set_size(m_p_pager, LV_SIZE_CONTENT, LV_SIZE_CONTENT);
    (void)flat_button(m_p_pager, LV_SYMBOL_LEFT, &FilesApp::on_page, this, 0U);
    m_p_page_label = text(m_p_pager, "", COLOR_DIM, fw::theme::font_small());
    (void)flat_button(m_p_pager, LV_SYMBOL_RIGHT, &FilesApp::on_page, this, 1U);
}

// ============================================================================
// Điều hướng & dữ liệu
// ============================================================================
void FilesApp::open_location(Location location)
{
    m_location = location;
    for (size_t index = 0U; index < static_cast<size_t>(Location::COUNT); ++index) {
        if (index == static_cast<size_t>(location)) {
            lv_obj_add_state(m_p_side_items[index], LV_STATE_CHECKED);
        } else {
            lv_obj_remove_state(m_p_side_items[index], LV_STATE_CHECKED);
        }
    }
    const char* p_path = "";
    if (location == Location::APPS) {
        p_path = DIR_APPS;
    } else if (location == Location::INSTALL) {
        p_path = DIR_INSTALL;
        if (!context().services.files().exists(DIR_INSTALL)) {
            (void)context().services.files().make_dir(DIR_INSTALL);
        }
    } else if (location == Location::TRASH) {
        p_path = DIR_TRASH;
    } else {
        // HOME, RECENT
    }
    m_back_count = 0U;
    m_forward_count = 0U;
    navigate(p_path, false);
}

void FilesApp::navigate(const char* p_path, bool push_history)
{
    if (push_history && (std::strcmp(p_path, m_path) != 0)) {
        if (m_back_count == HISTORY_MAX) {
            std::memmove(&m_back[0], &m_back[1], sizeof(m_back[0]) * (HISTORY_MAX - 1U));
            --m_back_count;
        }
        (void)common::copy_string(m_back[m_back_count++], sizeof(m_back[0]), m_path);
        m_forward_count = 0U;
    }
    (void)common::copy_string(m_path, sizeof(m_path), p_path);
    // Ra khỏi thư mục đặc biệt bằng breadcrumb -> sidebar về Home.
    if ((m_location != Location::RECENT) && (m_location != Location::HOME)) {
        const char* const p_root = (m_location == Location::APPS)      ? DIR_APPS
                                   : (m_location == Location::INSTALL) ? DIR_INSTALL
                                                                       : DIR_TRASH;
        if (std::strncmp(m_path, p_root, std::strlen(p_root)) != 0) {
            m_location = Location::HOME;
            for (size_t index = 0U; index < static_cast<size_t>(Location::COUNT); ++index) {
                if (index == static_cast<size_t>(Location::HOME)) {
                    lv_obj_add_state(m_p_side_items[index], LV_STATE_CHECKED);
                } else {
                    lv_obj_remove_state(m_p_side_items[index], LV_STATE_CHECKED);
                }
            }
        }
    }
    m_page = 0U;
    reload();
    lv_obj_scroll_to_y(m_p_list, 0, LV_ANIM_OFF);
}

void FilesApp::history_step(bool is_forward)
{
    Path* const p_from = is_forward ? m_forward : m_back;
    size_t& from_count = is_forward ? m_forward_count : m_back_count;
    Path* const p_to = is_forward ? m_back : m_forward;
    size_t& to_count = is_forward ? m_back_count : m_forward_count;
    if (from_count == 0U) {
        return;
    }
    if (to_count == HISTORY_MAX) {
        std::memmove(&p_to[0], &p_to[1], sizeof(p_to[0]) * (HISTORY_MAX - 1U));
        --to_count;
    }
    (void)common::copy_string(p_to[to_count++], sizeof(p_to[0]), m_path);
    --from_count;
    char target[fw::FILE_PATH_BUF_LEN];
    (void)common::copy_string(target, sizeof(target), p_from[from_count]);
    if (m_location == Location::RECENT) {
        m_location = Location::HOME;
    }
    navigate(target, false);
}

void FilesApp::reload()
{
    clear_selection();
    if (m_location == Location::RECENT) {
        load_recent_entries();
    } else {
        load_entries();
    }
    sort_entries();
    render();
}

void FilesApp::load_entries()
{
    CollectCtx ctx{m_entries, 0U, MAX_ENTRIES, m_show_hidden, false};
    fw::FileService& files = context().services.files();
    if (files.is_available()) {
        if (!files.exists(m_path) && (m_location == Location::TRASH)) {
            (void)files.make_dir(DIR_SYSTEM);
            (void)files.make_dir(DIR_TRASH);
        }
        (void)files.list(m_path, &collect, &ctx);
    }
    m_count = ctx.count;
    m_truncated = ctx.truncated;
}

void FilesApp::load_recent_entries()
{
    fw::FileService& files = context().services.files();
    m_count = 0U;
    size_t kept = 0U;
    for (size_t index = 0U; index < m_recent_count; ++index) {
        fw::FileEntry info{};
        if (!common::is_ok(files.stat(m_recent[index], info))) {
            continue; // File đã bị xóa / đổi tên: bỏ khỏi danh sách.
        }
        if (kept != index) {
            (void)common::copy_string(m_recent[kept], sizeof(m_recent[0]), m_recent[index]);
        }
        m_entries[kept] = info;
        ++kept;
    }
    if (kept != m_recent_count) {
        m_recent_count = kept;
        save_recent();
    }
    m_count = kept;
    m_truncated = false;
}

void FilesApp::sort_entries()
{
    for (size_t index = 0U; index < m_count; ++index) {
        m_order[index] = static_cast<uint16_t>(index);
    }
    if (m_location == Location::RECENT) {
        return; // Giữ thứ tự mới nhất trước.
    }
    const auto before = [this](const fw::FileEntry& a, const fw::FileEntry& b) {
        if (a.is_dir != b.is_dir) {
            return a.is_dir; // Thư mục luôn đứng trước.
        }
        int cmp = 0;
        if (m_sort == SortKey::SIZE) {
            cmp = (a.size < b.size) ? -1 : ((a.size > b.size) ? 1 : 0);
        } else if (m_sort == SortKey::MODIFIED) {
            cmp = (a.mtime < b.mtime) ? -1 : ((a.mtime > b.mtime) ? 1 : 0);
        } else {
            cmp = 0;
        }
        if (cmp == 0) {
            cmp = compare_nocase(a.name, b.name);
        }
        return m_sort_desc ? (cmp > 0) : (cmp < 0);
    };
    for (size_t index = 1U; index < m_count; ++index) { // Chèn: n <= 512, ổn định.
        const uint16_t key = m_order[index];
        size_t pos = index;
        while ((pos > 0U) && before(m_entries[key], m_entries[m_order[pos - 1U]])) {
            m_order[pos] = m_order[pos - 1U];
            --pos;
        }
        m_order[pos] = key;
    }
}

bool FilesApp::entry_path(size_t entry, char* p_out, size_t out_len) const
{
    if (m_location == Location::RECENT) {
        return common::copy_string(p_out, out_len, m_recent[entry]) == std::strlen(m_recent[entry]);
    }
    return fw::FileService::join(p_out, out_len, m_path, m_entries[entry].name);
}

const char* FilesApp::display_name(size_t entry) const
{
    const char* const p_name = m_entries[entry].name;
    const bool in_trash_root = (m_location == Location::TRASH) && (std::strcmp(m_path, DIR_TRASH) == 0);
    return in_trash_root ? strip_trash_prefix(p_name) : p_name;
}

bool FilesApp::is_protected(const char* p_path) const
{
    // Thư mục app đã cài do app "Apps" quản lý; thư mục hệ thống của File Manager không được sửa trực tiếp.
    const auto under = [p_path](const char* p_root) {
        const size_t len = std::strlen(p_root);
        return (std::strncmp(p_path, p_root, len) == 0) && ((p_path[len] == '\0') || (p_path[len] == '/'));
    };
    return under(DIR_APPS) || (std::strcmp(p_path, DIR_INSTALL) == 0) || (std::strcmp(p_path, DIR_SYSTEM) == 0) ||
           (std::strcmp(p_path, DIR_TRASH) == 0) || under(DIR_TRASH_INFO);
}

// ============================================================================
// Hiển thị
// ============================================================================
void FilesApp::render()
{
    lv_obj_clean(m_p_list);
    for (lv_obj_t*& p_item : m_p_items) {
        p_item = nullptr;
    }
    if (m_is_grid) {
        lv_obj_set_flex_flow(m_p_list, LV_FLEX_FLOW_ROW_WRAP);
        lv_obj_set_flex_align(m_p_list, LV_FLEX_ALIGN_START, LV_FLEX_ALIGN_START, LV_FLEX_ALIGN_START);
    } else {
        lv_obj_set_flex_flow(m_p_list, LV_FLEX_FLOW_COLUMN);
        lv_obj_set_flex_align(m_p_list, LV_FLEX_ALIGN_START, LV_FLEX_ALIGN_START, LV_FLEX_ALIGN_START);
    }
    lv_obj_set_hidden(m_p_header, m_is_grid);

    const size_t first = m_page * PAGE_SIZE;
    for (size_t slot = 0U; (slot < PAGE_SIZE) && ((first + slot) < m_count); ++slot) {
        const size_t entry = m_order[first + slot];
        if (m_is_grid) {
            render_tile(m_p_list, entry, slot);
        } else {
            render_row(m_p_list, entry, slot);
        }
    }
    if (m_count == 0U) {
        const char* p_empty = "Folder is empty";
        if (!context().services.files().is_available()) {
            p_empty = LV_SYMBOL_WARNING "  No SD card";
        } else if (m_location == Location::TRASH) {
            p_empty = "Trash is empty";
        } else if (m_location == Location::RECENT) {
            p_empty = "No recently opened files";
        } else {
            // Thư mục rỗng.
        }
        lv_obj_t* const p_label = text(m_p_list, p_empty, COLOR_DIM, fw::theme::font_medium());
        lv_obj_set_width(p_label, LV_PCT(100));
        lv_obj_set_style_text_align(p_label, LV_TEXT_ALIGN_CENTER, 0);
        lv_obj_set_style_pad_top(p_label, fw::theme::px(ROW_H * 2), 0);
    }
    lv_label_set_text_static(lv_obj_get_child(m_p_view_btn, 0), m_is_grid ? LV_SYMBOL_LIST : LV_SYMBOL_IMAGE);
    render_breadcrumb();
    render_header();
    render_footer();
}

void FilesApp::render_row(lv_obj_t* p_list, size_t entry, size_t slot)
{
    const fw::FileEntry& info = m_entries[entry];
    lv_obj_t* const p_row = flex(p_list, LV_FLEX_FLOW_ROW, 12);
    lv_obj_set_size(p_row, LV_PCT(100), fw::theme::px(ROW_H));
    lv_obj_set_style_pad_hor(p_row, fw::theme::px(PAD), 0);
    lv_obj_set_style_radius(p_row, fw::theme::px(RADIUS), 0);
    lv_obj_set_style_bg_color(p_row, lv_color_hex(COLOR_ROW_PRESSED), LV_STATE_PRESSED);
    lv_obj_set_style_bg_opa(p_row, LV_OPA_COVER, LV_STATE_PRESSED);
    lv_obj_set_style_bg_color(p_row, lv_color_hex(fw::palette::ACCENT), LV_STATE_CHECKED);
    lv_obj_set_style_bg_opa(p_row, SELECTED_OPA, LV_STATE_CHECKED);
    lv_obj_set_clickable(p_row, true);
    lv_obj_set_user_data(p_row, reinterpret_cast<void*>(entry));
    lv_obj_add_event_cb(p_row, &FilesApp::on_item_clicked, LV_EVENT_CLICKED, this);
    lv_obj_add_event_cb(p_row, &FilesApp::on_item_long_pressed, LV_EVENT_LONG_PRESSED, this);

    const char* p_symbol = nullptr;
    uint32_t color = 0U;
    icon_for(info, p_symbol, color);
    lv_obj_t* const p_icon = plain(p_row);
    lv_obj_set_size(p_icon, fw::theme::px(ROW_ICON), fw::theme::px(ROW_ICON));
    lv_obj_set_style_radius(p_icon, fw::theme::px(RADIUS - 2), 0);
    lv_obj_set_style_bg_color(p_icon, lv_color_hex(COLOR_TILE), 0);
    lv_obj_set_style_bg_opa(p_icon, LV_OPA_COVER, 0);
    lv_obj_t* const p_glyph = text(p_icon, p_symbol, color, fw::theme::font_small());
    lv_obj_center(p_glyph);

    lv_obj_t* const p_name = text(p_row, display_name(entry), COLOR_TEXT, fw::theme::font_small());
    lv_obj_set_flex_grow(p_name, 1);
    lv_label_set_long_mode(p_name, LV_LABEL_LONG_DOT);

    char buf[32];
    if (info.is_dir) {
        char path[fw::FILE_PATH_BUF_LEN];
        size_t children = 0U;
        if (entry_path(entry, path, sizeof(path))) {
            (void)context().services.files().list(path, &count_child, &children);
        }
        (void)std::snprintf(buf, sizeof(buf), "%u item%s", static_cast<unsigned>(children), (children == 1U) ? "" : "s");
    } else {
        format_size(info.size, buf, sizeof(buf));
    }
    lv_obj_t* const p_size = text(p_row, buf, COLOR_DIM, fw::theme::font_small());
    lv_obj_set_width(p_size, fw::theme::px(COL_SIZE_W));
    format_time(info.mtime, context().services.time().utc_offset_minutes(), buf, sizeof(buf));
    lv_obj_t* const p_date = text(p_row, buf, COLOR_DIM, fw::theme::font_small());
    lv_obj_set_width(p_date, fw::theme::px(COL_DATE_W));

    m_p_items[slot] = p_row;
}

void FilesApp::render_tile(lv_obj_t* p_list, size_t entry, size_t slot)
{
    const fw::FileEntry& info = m_entries[entry];
    lv_obj_t* const p_tile = flex(p_list, LV_FLEX_FLOW_COLUMN, 6);
    lv_obj_set_size(p_tile, fw::theme::px(TILE_W), LV_SIZE_CONTENT);
    lv_obj_set_style_pad_all(p_tile, fw::theme::px(GAP), 0);
    lv_obj_set_style_radius(p_tile, fw::theme::px(RADIUS), 0);
    lv_obj_set_style_bg_color(p_tile, lv_color_hex(COLOR_ROW_PRESSED), LV_STATE_PRESSED);
    lv_obj_set_style_bg_opa(p_tile, LV_OPA_COVER, LV_STATE_PRESSED);
    lv_obj_set_style_bg_color(p_tile, lv_color_hex(fw::palette::ACCENT), LV_STATE_CHECKED);
    lv_obj_set_style_bg_opa(p_tile, SELECTED_OPA, LV_STATE_CHECKED);
    lv_obj_set_clickable(p_tile, true);
    lv_obj_set_user_data(p_tile, reinterpret_cast<void*>(entry));
    lv_obj_add_event_cb(p_tile, &FilesApp::on_item_clicked, LV_EVENT_CLICKED, this);
    lv_obj_add_event_cb(p_tile, &FilesApp::on_item_long_pressed, LV_EVENT_LONG_PRESSED, this);

    const char* p_symbol = nullptr;
    uint32_t color = 0U;
    icon_for(info, p_symbol, color);
    lv_obj_t* const p_icon = plain(p_tile);
    lv_obj_set_size(p_icon, fw::theme::px(TILE_ICON), fw::theme::px(TILE_ICON));
    lv_obj_set_style_radius(p_icon, fw::theme::px(RADIUS + 2), 0);
    lv_obj_set_style_bg_color(p_icon, lv_color_hex(COLOR_TILE), 0);
    lv_obj_set_style_bg_opa(p_icon, LV_OPA_COVER, 0);
    lv_obj_t* const p_glyph = text(p_icon, p_symbol, color, fw::theme::font_medium());
    lv_obj_center(p_glyph);

    lv_obj_t* const p_name = text(p_tile, display_name(entry), COLOR_TEXT, fw::theme::font_small());
    lv_obj_set_size(p_name, LV_PCT(100), fw::theme::px(TILE_NAME_H));
    lv_obj_set_style_text_align(p_name, LV_TEXT_ALIGN_CENTER, 0);
    lv_label_set_long_mode(p_name, LV_LABEL_LONG_DOT);

    m_p_items[slot] = p_tile;
}

void FilesApp::render_breadcrumb()
{
    lv_obj_clean(m_p_crumbs);
    const auto crumb = [this](const char* p_label, size_t prefix_len, bool is_last) {
        lv_obj_t* const p_crumb = text(m_p_crumbs, p_label, is_last ? COLOR_TEXT : COLOR_DIM, fw::theme::font_small());
        lv_obj_set_style_pad_hor(p_crumb, fw::theme::px(4), 0);
        lv_obj_set_clickable(p_crumb, !is_last);
        lv_obj_set_user_data(p_crumb, reinterpret_cast<void*>(prefix_len));
        lv_obj_add_event_cb(p_crumb, &FilesApp::on_crumb, LV_EVENT_CLICKED, this);
    };
    if (m_location == Location::RECENT) {
        crumb(LV_SYMBOL_REFRESH "  Recent", 0U, true);
        return;
    }
    const bool in_trash = (m_location == Location::TRASH);
    const size_t root_len = in_trash ? std::strlen(DIR_TRASH) : 0U;
    crumb(in_trash ? (LV_SYMBOL_TRASH "  Trash") : (LV_SYMBOL_HOME "  Home"), root_len,
          std::strlen(m_path) <= root_len);
    // Thành phần sau gốc; quá nhiều thì chỉ giữ 3 cấp cuối.
    constexpr size_t MAX_SHOWN = 3U;
    size_t total = 0U;
    for (size_t index = root_len; m_path[index] != '\0'; ++index) {
        if ((index == root_len) || (m_path[index] == '/')) {
            ++total;
        }
    }
    size_t part = 0U;
    size_t start = (root_len > 0U) ? (root_len + 1U) : 0U;
    while (start < std::strlen(m_path) + 1U && m_path[start - ((start > 0U) ? 1U : 0U)] != '\0') {
        const char* const p_slash = std::strchr(&m_path[start], '/');
        const size_t end = (p_slash != nullptr) ? static_cast<size_t>(p_slash - m_path) : std::strlen(m_path);
        if (end <= start) {
            break;
        }
        ++part;
        if ((total <= MAX_SHOWN) || (part > (total - MAX_SHOWN))) {
            char name[fw::FILE_NAME_MAX_LEN + 1U];
            const size_t len = ((end - start) < sizeof(name)) ? (end - start) : (sizeof(name) - 1U);
            std::memcpy(name, &m_path[start], len);
            name[len] = '\0';
            (void)text(m_p_crumbs, LV_SYMBOL_RIGHT, COLOR_DIM, fw::theme::font_small());
            crumb(name, end, p_slash == nullptr);
        } else if (part == 1U) {
            (void)text(m_p_crumbs, LV_SYMBOL_RIGHT "  ...", COLOR_DIM, fw::theme::font_small());
        } else {
            // Ẩn cấp giữa.
        }
        if (p_slash == nullptr) {
            break;
        }
        start = end + 1U;
    }
    set_enabled(m_p_back_btn, m_back_count > 0U);
    set_enabled(m_p_fwd_btn, m_forward_count > 0U);
}

void FilesApp::render_header()
{
    static const char* const TITLES[] = {"Name", "Size", "Modified"};
    const size_t active = static_cast<size_t>(m_sort);
    for (size_t index = 0U; index < 3U; ++index) {
        if (index == active) {
            lv_label_set_text_fmt(m_p_header_cells[index], "%s  %s", TITLES[index],
                                  m_sort_desc ? LV_SYMBOL_DOWN : LV_SYMBOL_UP);
        } else {
            lv_label_set_text_static(m_p_header_cells[index], TITLES[index]);
        }
    }
}

void FilesApp::render_footer()
{
    lv_obj_clean(m_p_actions);
    const bool is_busy = (m_queue_kind != QueueKind::NONE);
    if (is_busy) {
        (void)flat_button(m_p_actions, "Cancel", &FilesApp::on_action, this, ACT_CANCEL_JOB);
    } else if (m_selected_count > 0U) {
        const bool in_trash = (m_location == Location::TRASH);
        if (in_trash) {
            (void)flat_button(m_p_actions, LV_SYMBOL_UPLOAD " Restore", &FilesApp::on_action, this, ACT_RESTORE);
            lv_obj_t* const p_del =
                flat_button(m_p_actions, LV_SYMBOL_TRASH " Delete forever", &FilesApp::on_action, this, ACT_FOREVER);
            lv_obj_set_style_text_color(p_del, lv_color_hex(fw::palette::DESTRUCTIVE), 0);
        } else {
            (void)flat_button(m_p_actions, LV_SYMBOL_COPY " Copy", &FilesApp::on_action, this, ACT_COPY);
            (void)flat_button(m_p_actions, LV_SYMBOL_CUT " Cut", &FilesApp::on_action, this, ACT_CUT);
            if (m_selected_count == 1U) {
                (void)flat_button(m_p_actions, LV_SYMBOL_EDIT " Rename", &FilesApp::on_action, this, ACT_RENAME);
            }
            lv_obj_t* const p_del =
                flat_button(m_p_actions, LV_SYMBOL_TRASH " Delete", &FilesApp::on_action, this, ACT_DELETE);
            lv_obj_set_style_text_color(p_del, lv_color_hex(fw::palette::DESTRUCTIVE), 0);
        }
        (void)flat_button(m_p_actions, "Select all", &FilesApp::on_action, this, ACT_SELECT_ALL);
        (void)flat_button(m_p_actions, LV_SYMBOL_CLOSE, &FilesApp::on_action, this, ACT_CANCEL_SEL);
    } else {
        // Không chọn gì: chỉ dòng trạng thái.
    }

    if (is_busy) {
        char done[24];
        format_size(context().services.files().job_bytes(), done, sizeof(done));
        const char* const p_verb = (m_queue_kind == QueueKind::DELETE) ? "Deleting" : "Copying";
        (void)std::snprintf(m_status, sizeof(m_status), "%s %u/%u  -  %u item(s), %s", p_verb,
                            static_cast<unsigned>(m_queue_next), static_cast<unsigned>(m_queue_count),
                            static_cast<unsigned>(m_queue_done + context().services.files().job_items()), done);
    } else if (m_selected_count > 0U) {
        (void)std::snprintf(m_status, sizeof(m_status), "%u selected", static_cast<unsigned>(m_selected_count));
    } else if (m_status[0] == '\0') {
        size_t folders = 0U;
        for (size_t index = 0U; index < m_count; ++index) {
            folders += m_entries[index].is_dir ? 1U : 0U;
        }
        (void)std::snprintf(m_status, sizeof(m_status), "%u folder(s), %u file(s)%s", static_cast<unsigned>(folders),
                            static_cast<unsigned>(m_count - folders), m_truncated ? "  (list truncated)" : "");
    } else {
        // Giữ thông báo của thao tác vừa xong; xóa ở lần render sau.
    }
    lv_label_set_text(m_p_footer_label, m_status);
    m_status[0] = '\0';

    const size_t pages = (m_count + PAGE_SIZE - 1U) / PAGE_SIZE;
    lv_obj_set_hidden(m_p_pager, pages <= 1U);
    if (pages > 1U) {
        const size_t first = m_page * PAGE_SIZE;
        const size_t last = ((first + PAGE_SIZE) < m_count) ? (first + PAGE_SIZE) : m_count;
        lv_label_set_text_fmt(m_p_page_label, "%u-%u of %u", static_cast<unsigned>(first + 1U),
                              static_cast<unsigned>(last), static_cast<unsigned>(m_count));
        set_enabled(lv_obj_get_child(m_p_pager, 0), m_page > 0U);
        set_enabled(lv_obj_get_child(m_p_pager, 2), (m_page + 1U) < pages);
    }
    const bool can_paste = (m_clip_count > 0U) && !is_busy && (m_location != Location::RECENT) &&
                           (m_location != Location::TRASH) && !is_protected(m_path);
    lv_obj_set_hidden(m_p_paste_btn, !can_paste);
    if (can_paste) {
        lv_label_set_text_fmt(lv_obj_get_child(m_p_paste_btn, 0), LV_SYMBOL_PASTE " Paste %u",
                              static_cast<unsigned>(m_clip_count));
    }
}

void FilesApp::render_storage()
{
    uint64_t total = 0U;
    uint64_t free_bytes = 0U;
    if (!context().services.files().capacity(total, free_bytes) || (total == 0U)) {
        lv_label_set_text(m_p_storage_label, "Not available");
        lv_bar_set_value(m_p_storage_bar, 0, LV_ANIM_OFF);
        return;
    }
    constexpr uint64_t PERCENT = 100U;
    lv_bar_set_value(m_p_storage_bar, static_cast<int32_t>(((total - free_bytes) * PERCENT) / total), LV_ANIM_OFF);
    char free_text[24];
    char total_text[24];
    format_size(free_bytes, free_text, sizeof(free_text));
    format_size(total, total_text, sizeof(total_text));
    lv_label_set_text_fmt(m_p_storage_label, "%s free of %s", free_text, total_text);
}

void FilesApp::update_selection_style()
{
    for (lv_obj_t* const p_item : m_p_items) {
        if (p_item == nullptr) {
            continue;
        }
        const auto entry = reinterpret_cast<uintptr_t>(lv_obj_get_user_data(p_item));
        if (m_selected[entry]) {
            lv_obj_add_state(p_item, LV_STATE_CHECKED);
        } else {
            lv_obj_remove_state(p_item, LV_STATE_CHECKED);
        }
    }
    render_footer();
}

// ============================================================================
// Chọn & thao tác
// ============================================================================
void FilesApp::toggle_select(size_t entry)
{
    m_selected[entry] = !m_selected[entry];
    m_selected_count = m_selected[entry] ? (m_selected_count + 1U) : (m_selected_count - 1U);
    update_selection_style();
}

void FilesApp::clear_selection()
{
    std::memset(m_selected, 0, sizeof(m_selected));
    m_selected_count = 0U;
    if (m_p_actions != nullptr) {
        update_selection_style();
    }
}

void FilesApp::open_entry(size_t entry)
{
    char path[fw::FILE_PATH_BUF_LEN];
    if (!entry_path(entry, path, sizeof(path))) {
        return;
    }
    const fw::FileEntry& info = m_entries[entry];
    if (info.is_dir) {
        if (m_location == Location::RECENT) {
            m_location = Location::HOME;
        }
        navigate(path, true);
        return;
    }
    if (m_location == Location::TRASH) {
        show_info(path, info); // Trong thùng rác chỉ xem thông tin.
        return;
    }
    add_recent(path);
    if (has_ext(info.name, EXT_GAPP)) {
        ask_install(path);
    } else if (is_text_file(info.name)) {
        open_text(path);
    } else {
        show_info(path, info);
    }
}

void FilesApp::open_text(const char* p_path)
{
    size_t len = 0U;
    const common::Status status = context().services.files().read_file(
        p_path, reinterpret_cast<uint8_t*>(m_text), TEXT_MAX, len);
    if (!common::is_ok(status)) {
        set_status("Cannot read file");
        render_footer();
        return;
    }
    for (size_t index = 0U; index < len; ++index) {
        const auto c = static_cast<unsigned char>(m_text[index]);
        constexpr unsigned char FIRST_PRINTABLE = 0x20U;
        if ((c < FIRST_PRINTABLE) && (c != '\n')) {
            m_text[index] = ' '; // Tab, CR, ký tự điều khiển -> dấu cách (font không có glyph).
        }
    }
    m_text[len] = '\0';

    m_p_viewer = overlay(LV_OPA_COVER);
    lv_obj_set_style_bg_color(m_p_viewer, lv_color_hex(COLOR_MAIN_BG), 0);
    lv_obj_set_flex_flow(m_p_viewer, LV_FLEX_FLOW_COLUMN);
    lv_obj_set_style_pad_all(m_p_viewer, fw::theme::px(PAD), 0);
    lv_obj_set_style_pad_gap(m_p_viewer, fw::theme::px(GAP), 0);
    lv_obj_t* const p_head = flex(m_p_viewer, LV_FLEX_FLOW_ROW, GAP);
    lv_obj_set_width(p_head, LV_PCT(100));
    (void)flat_button(p_head, LV_SYMBOL_LEFT " Close", &FilesApp::on_viewer_close, this, 0U);
    lv_obj_t* const p_title = text(p_head, p_path, COLOR_TEXT, fw::theme::font_small());
    lv_obj_set_flex_grow(p_title, 1);
    lv_label_set_long_mode(p_title, LV_LABEL_LONG_DOT);
    if (len >= TEXT_MAX) {
        (void)text(p_head, "(first 16 KB)", COLOR_DIM, fw::theme::font_small());
    }
    lv_obj_t* const p_scroll = lv_obj_create(m_p_viewer);
    lv_obj_remove_style_all(p_scroll);
    lv_obj_set_width(p_scroll, LV_PCT(100));
    lv_obj_set_flex_grow(p_scroll, 1);
    lv_obj_set_clickable(p_scroll, true);
    lv_obj_set_scroll_dir(p_scroll, LV_DIR_VER);
    lv_obj_set_style_bg_color(p_scroll, lv_color_hex(COLOR_SIDE_BG), 0);
    lv_obj_set_style_bg_opa(p_scroll, LV_OPA_COVER, 0);
    lv_obj_set_style_radius(p_scroll, fw::theme::px(RADIUS), 0);
    lv_obj_set_style_pad_all(p_scroll, fw::theme::px(PAD), 0);
    lv_obj_t* const p_body = text(p_scroll, (len == 0U) ? "(empty file)" : m_text, COLOR_TEXT, fw::theme::font_small());
    lv_obj_set_width(p_body, LV_PCT(100));
    lv_label_set_long_mode(p_body, LV_LABEL_LONG_WRAP);
}

void FilesApp::show_info(const char* p_path, const fw::FileEntry& info)
{
    char size_text[24];
    char time_text[24];
    format_size(info.size, size_text, sizeof(size_text));
    format_time(info.mtime, context().services.time().utc_offset_minutes(), time_text, sizeof(time_text));
    char message[fw::FILE_PATH_BUF_LEN + 64U];
    (void)std::snprintf(message, sizeof(message), "Size: %s\nModified: %s\n%s", info.is_dir ? "-" : size_text,
                        time_text, p_path);
    const fw::alert::Action ok{"OK", fw::palette::ACCENT, &FilesApp::on_dialog_cancel};
    close_dialog();
    m_p_dialog = fw::alert::show(fw::FileService::base_name(p_path), message, &ok, 1U, this);
}

void FilesApp::ask_install(const char* p_path)
{
    (void)common::copy_string(m_target, sizeof(m_target), p_path);
    char title[fw::FILE_NAME_MAX_LEN + 16U];
    (void)std::snprintf(title, sizeof(title), "Install %s?", fw::FileService::base_name(p_path));
    close_dialog();
    m_p_dialog = fw::alert::show(title, "The app package will be installed from the SD card.",
                                 fw::alert::Action{"Cancel", fw::palette::ACCENT, &FilesApp::on_dialog_cancel},
                                 fw::alert::Action{"Install", fw::palette::ACCENT, &FilesApp::on_install_confirm},
                                 this);
}

void FilesApp::copy_selection(ClipOp op)
{
    m_clip_count = 0U;
    for (size_t index = 0U; (index < m_count) && (m_clip_count < QUEUE_MAX); ++index) {
        char path[fw::FILE_PATH_BUF_LEN];
        if (m_selected[index] && entry_path(index, path, sizeof(path))) {
            if ((op == ClipOp::CUT) && is_protected(path)) {
                continue;
            }
            (void)common::copy_string(m_clip[m_clip_count++], sizeof(m_clip[0]), path);
        }
    }
    m_clip_op = (m_clip_count > 0U) ? op : ClipOp::NONE;
    (void)std::snprintf(m_status, sizeof(m_status), "%u item(s) %s - open a folder and tap Paste",
                        static_cast<unsigned>(m_clip_count), (op == ClipOp::CUT) ? "cut" : "copied");
    clear_selection();
}

void FilesApp::paste()
{
    if (m_clip_count == 0U) {
        return;
    }
    (void)common::copy_string(m_queue_dir, sizeof(m_queue_dir), m_path);
    if (m_clip_op == ClipOp::CUT) {
        size_t moved = 0U;
        for (size_t index = 0U; index < m_clip_count; ++index) {
            char target[fw::FILE_PATH_BUF_LEN];
            if (unique_path(m_path, fw::FileService::base_name(m_clip[index]), target, sizeof(target)) &&
                common::is_ok(context().services.files().rename(m_clip[index], target))) {
                ++moved;
            }
        }
        (void)std::snprintf(m_status, sizeof(m_status), "Moved %u of %u item(s)", static_cast<unsigned>(moved),
                            static_cast<unsigned>(m_clip_count));
        m_clip_count = 0U;
        m_clip_op = ClipOp::NONE;
        reload();
        return;
    }
    m_queue_count = m_clip_count;
    for (size_t index = 0U; index < m_clip_count; ++index) {
        (void)common::copy_string(m_queue[index], sizeof(m_queue[0]), m_clip[index]);
    }
    start_queue(QueueKind::PASTE_COPY);
}

bool FilesApp::unique_path(const char* p_dir, const char* p_name, char* p_out, size_t out_len) const
{
    const fw::FileService& files = context().services.files();
    if (!fw::FileService::join(p_out, out_len, p_dir, p_name)) {
        return false;
    }
    if (!files.exists(p_out)) {
        return true;
    }
    // "tên (2).ext", "tên (3).ext"...
    const char* const p_dot = std::strrchr(p_name, '.');
    const size_t stem_len = ((p_dot != nullptr) && (p_dot != p_name)) ? static_cast<size_t>(p_dot - p_name)
                                                                         : std::strlen(p_name);
    const char* const p_ext = ((p_dot != nullptr) && (p_dot != p_name)) ? p_dot : "";
    for (int suffix = 2; suffix <= MAX_UNIQUE_SUFFIX; ++suffix) {
        char name[fw::FILE_NAME_MAX_LEN + 1U];
        const int written = std::snprintf(name, sizeof(name), "%.*s (%d)%s", static_cast<int>(stem_len), p_name,
                                          suffix, p_ext);
        if ((written > 0) && (static_cast<size_t>(written) < sizeof(name)) &&
            fw::FileService::join(p_out, out_len, p_dir, name) && !files.exists(p_out)) {
            return true;
        }
    }
    return false;
}

bool FilesApp::move_to_trash(const char* p_path)
{
    fw::FileService& files = context().services.files();
    (void)files.make_dir(DIR_SYSTEM);
    (void)files.make_dir(DIR_TRASH);
    (void)files.make_dir(DIR_TRASH_INFO);
    const char* const p_name = fw::FileService::base_name(p_path);
    for (unsigned serial = 1U; serial < static_cast<unsigned>(UINT16_MAX); ++serial) {
        char trash_name[fw::FILE_NAME_MAX_LEN + 1U];
        const int written = std::snprintf(trash_name, sizeof(trash_name), "%u-%s", serial, p_name);
        if ((written <= 0) || (static_cast<size_t>(written) >= sizeof(trash_name))) {
            return false;
        }
        char target[fw::FILE_PATH_BUF_LEN];
        if (!fw::FileService::join(target, sizeof(target), DIR_TRASH, trash_name)) {
            return false;
        }
        if (files.exists(target)) {
            continue;
        }
        if (!common::is_ok(files.rename(p_path, target))) {
            return false;
        }
        char info_path[fw::FILE_PATH_BUF_LEN];
        if (fw::FileService::join(info_path, sizeof(info_path), DIR_TRASH_INFO, trash_name)) {
            (void)files.write_file(info_path, reinterpret_cast<const uint8_t*>(p_path), std::strlen(p_path));
        }
        return true;
    }
    return false;
}

void FilesApp::trash_selection()
{
    size_t moved = 0U;
    size_t skipped = 0U;
    for (size_t index = 0U; index < m_count; ++index) {
        char path[fw::FILE_PATH_BUF_LEN];
        if (!m_selected[index] || !entry_path(index, path, sizeof(path))) {
            continue;
        }
        if (is_protected(path) || std::strncmp(path, DIR_TRASH, std::strlen(DIR_TRASH)) == 0) {
            ++skipped;
            continue;
        }
        if (move_to_trash(path)) {
            ++moved;
        }
    }
    if (skipped > 0U) {
        (void)std::snprintf(m_status, sizeof(m_status), "Moved %u item(s) to Trash, %u protected item(s) skipped",
                            static_cast<unsigned>(moved), static_cast<unsigned>(skipped));
    } else {
        (void)std::snprintf(m_status, sizeof(m_status), "Moved %u item(s) to Trash", static_cast<unsigned>(moved));
    }
    reload();
    render_storage();
}

void FilesApp::restore_selection()
{
    fw::FileService& files = context().services.files();
    size_t restored = 0U;
    size_t failed = 0U;
    for (size_t index = 0U; index < m_count; ++index) {
        if (!m_selected[index]) {
            continue;
        }
        char info_path[fw::FILE_PATH_BUF_LEN];
        char trash_path[fw::FILE_PATH_BUF_LEN];
        char original[fw::FILE_PATH_BUF_LEN] = {};
        size_t len = 0U;
        const bool has_info = fw::FileService::join(info_path, sizeof(info_path), DIR_TRASH_INFO,
                                                     m_entries[index].name) &&
                              common::is_ok(files.read_file(info_path, reinterpret_cast<uint8_t*>(original),
                                                            sizeof(original) - 1U, len));
        original[len] = '\0';
        const char* p_dest = original;
        char fallback[fw::FILE_PATH_BUF_LEN];
        if (!has_info || (len == 0U) || files.exists(original)) {
            // Không rõ nơi gốc hoặc đã có file trùng tên: khôi phục về gốc thẻ với tên không trùng.
            if (!unique_path("", strip_trash_prefix(m_entries[index].name), fallback, sizeof(fallback))) {
                ++failed;
                continue;
            }
            p_dest = fallback;
        }
        if (fw::FileService::join(trash_path, sizeof(trash_path), DIR_TRASH, m_entries[index].name) &&
            common::is_ok(files.rename(trash_path, p_dest))) {
            ++restored;
            if (has_info) {
                (void)files.start_delete(info_path); // File thông tin: xóa ngay (một file, không cần bước).
            }
        } else {
            ++failed;
        }
    }
    (void)std::snprintf(m_status, sizeof(m_status), "Restored %u item(s)%s", static_cast<unsigned>(restored),
                        (failed > 0U) ? ", some failed" : "");
    reload();
}

void FilesApp::erase_selection_forever()
{
    m_queue_count = 0U;
    for (size_t index = 0U; (index < m_count) && (m_queue_count < QUEUE_MAX); ++index) {
        if (m_selected[index] && entry_path(index, m_queue[m_queue_count], sizeof(m_queue[0]))) {
            ++m_queue_count;
        }
    }
    start_queue(QueueKind::DELETE);
}

void FilesApp::empty_trash()
{
    m_queue_count = 0U;
    (void)common::copy_string(m_queue[m_queue_count++], sizeof(m_queue[0]), DIR_TRASH);
    (void)common::copy_string(m_queue[m_queue_count++], sizeof(m_queue[0]), DIR_TRASH_INFO);
    start_queue(QueueKind::DELETE);
}

void FilesApp::start_queue(QueueKind kind)
{
    if ((m_queue_count == 0U) || (m_queue_kind != QueueKind::NONE)) {
        return;
    }
    m_queue_kind = kind;
    m_queue_next = 0U;
    m_queue_done = 0U;
    m_queue_failed = 0U;
    clear_selection();
    if (m_p_job_timer == nullptr) {
        m_p_job_timer = lv_timer_create(&FilesApp::on_job_timer, JOB_PERIOD_MS, this);
    }
    render_footer();
}

void FilesApp::finish_queue(bool is_ok)
{
    if (m_p_job_timer != nullptr) {
        lv_timer_delete(m_p_job_timer);
        m_p_job_timer = nullptr;
    }
    const QueueKind kind = m_queue_kind;
    m_queue_kind = QueueKind::NONE;
    if (kind == QueueKind::PASTE_COPY) {
        (void)std::snprintf(m_status, sizeof(m_status), "Copied %u item(s)%s",
                            static_cast<unsigned>(m_queue_next - m_queue_failed),
                            (m_queue_failed > 0U) ? ", some failed" : "");
    } else if (kind == QueueKind::DELETE) {
        (void)std::snprintf(m_status, sizeof(m_status), "Deleted %u item(s)%s",
                            static_cast<unsigned>(m_queue_next - m_queue_failed),
                            (m_queue_failed > 0U) ? ", some failed" : "");
    } else if (kind == QueueKind::INSTALL) {
        size_t installed = 0U;
        const common::Status status = is_ok ? runtime::install_pending(installed) : common::Status::ERR_IO;
        const char* const p_error = runtime::last_error();
        (void)std::snprintf(m_status, sizeof(m_status), "%s",
                            (common::is_ok(status) && (installed > 0U)) ? "App installed - see the Home screen"
                            : (p_error[0] != '\0')                       ? p_error
                                                                         : "Install failed");
    } else {
        // Không có hàng đợi.
    }
    if (m_location == Location::TRASH) {
        (void)context().services.files().make_dir(DIR_TRASH); // Dọn thùng rác xóa cả thư mục.
    }
    reload();
    render_storage();
}

void FilesApp::add_recent(const char* p_path)
{
    size_t found = m_recent_count;
    for (size_t index = 0U; index < m_recent_count; ++index) {
        if (std::strcmp(m_recent[index], p_path) == 0) {
            found = index;
            break;
        }
    }
    const size_t shift = (found < m_recent_count) ? found : ((m_recent_count < RECENT_MAX) ? m_recent_count
                                                                                            : (RECENT_MAX - 1U));
    for (size_t index = shift; index > 0U; --index) {
        (void)common::copy_string(m_recent[index], sizeof(m_recent[0]), m_recent[index - 1U]);
    }
    (void)common::copy_string(m_recent[0], sizeof(m_recent[0]), p_path);
    if (found >= m_recent_count) {
        m_recent_count = (m_recent_count < RECENT_MAX) ? (m_recent_count + 1U) : RECENT_MAX;
    }
    save_recent();
}

void FilesApp::load_recent()
{
    m_recent_count = 0U;
    size_t len = 0U;
    if (!common::is_ok(context().services.files().read_file(FILE_RECENT, reinterpret_cast<uint8_t*>(m_text),
                                                           TEXT_MAX, len))) {
        return;
    }
    m_text[len] = '\0';
    char* p_line = m_text;
    while ((p_line != nullptr) && (*p_line != '\0') && (m_recent_count < RECENT_MAX)) {
        char* const p_end = std::strchr(p_line, '\n');
        if (p_end != nullptr) {
            *p_end = '\0';
        }
        if (p_line[0] != '\0') {
            (void)common::copy_string(m_recent[m_recent_count++], sizeof(m_recent[0]), p_line);
        }
        p_line = (p_end != nullptr) ? (p_end + 1) : nullptr;
    }
}

void FilesApp::save_recent()
{
    fw::FileService& files = context().services.files();
    if (!files.is_available()) {
        return;
    }
    (void)files.make_dir(DIR_SYSTEM);
    size_t used = 0U;
    for (size_t index = 0U; index < m_recent_count; ++index) {
        const int written = std::snprintf(&m_text[used], TEXT_MAX - used, "%s\n", m_recent[index]);
        if ((written <= 0) || (static_cast<size_t>(written) >= (TEXT_MAX - used))) {
            break;
        }
        used += static_cast<size_t>(written);
    }
    (void)files.write_file(FILE_RECENT, reinterpret_cast<const uint8_t*>(m_text), used);
}

void FilesApp::set_status(const char* p_text)
{
    (void)common::copy_string(m_status, sizeof(m_status), p_text);
}

// ============================================================================
// Lớp phủ
// ============================================================================
lv_obj_t* FilesApp::overlay(lv_opa_t backdrop_opa)
{
    lv_obj_t* const p_layer = lv_obj_create(m_p_root);
    lv_obj_remove_style_all(p_layer);
    lv_obj_set_floating(p_layer, true);
    lv_obj_set_size(p_layer, LV_PCT(100), LV_PCT(100));
    lv_obj_align(p_layer, LV_ALIGN_TOP_LEFT, 0, 0);
    lv_obj_set_style_bg_color(p_layer, lv_color_black(), 0);
    lv_obj_set_style_bg_opa(p_layer, backdrop_opa, 0);
    lv_obj_set_scrollable(p_layer, false);
    lv_obj_set_clickable(p_layer, true); // Chặn chạm xuống danh sách phía sau.
    return p_layer;
}

void FilesApp::open_menu()
{
    if (m_p_menu != nullptr) {
        close_menu();
        return;
    }
    m_p_menu = overlay(LV_OPA_TRANSP);
    lv_obj_add_event_cb(m_p_menu, &FilesApp::on_menu, LV_EVENT_CLICKED, this); // Chạm ra ngoài: đóng.
    lv_obj_t* const p_card = flex(m_p_menu, LV_FLEX_FLOW_COLUMN, 2);
    lv_obj_set_size(p_card, fw::theme::px(MENU_W), LV_SIZE_CONTENT);
    lv_obj_align(p_card, LV_ALIGN_TOP_RIGHT, -fw::theme::px(PAD), fw::theme::px(ICON_BTN + PAD + GAP));
    lv_obj_set_style_bg_color(p_card, lv_color_hex(fw::palette::SURFACE_HIGH), 0);
    lv_obj_set_style_bg_opa(p_card, LV_OPA_COVER, 0);
    lv_obj_set_style_radius(p_card, fw::theme::px(RADIUS), 0);
    lv_obj_set_style_pad_all(p_card, fw::theme::px(6), 0);
    lv_obj_set_style_shadow_width(p_card, fw::theme::px(24), 0);
    lv_obj_set_style_shadow_opa(p_card, LV_OPA_50, 0);
    lv_obj_set_clickable(p_card, true);

    const auto item = [this, p_card](const char* p_label, uintptr_t tag, bool is_enabled) {
        lv_obj_t* const p_item = flex(p_card, LV_FLEX_FLOW_ROW, GAP);
        lv_obj_set_width(p_item, LV_PCT(100));
        lv_obj_set_style_pad_all(p_item, fw::theme::px(PAD), 0);
        lv_obj_set_style_radius(p_item, fw::theme::px(RADIUS - 4), 0);
        lv_obj_set_style_bg_color(p_item, lv_color_hex(COLOR_SIDE_SELECTED), LV_STATE_PRESSED);
        lv_obj_set_style_bg_opa(p_item, LV_OPA_COVER, LV_STATE_PRESSED);
        lv_obj_set_style_opa(p_item, LV_OPA_40, LV_STATE_DISABLED);
        lv_obj_set_clickable(p_item, true);
        set_enabled(p_item, is_enabled);
        lv_obj_set_user_data(p_item, reinterpret_cast<void*>(tag));
        lv_obj_add_event_cb(p_item, &FilesApp::on_menu_item, LV_EVENT_CLICKED, this);
        (void)text(p_item, p_label, COLOR_TEXT, fw::theme::font_small());
    };
    const bool can_create = (m_location != Location::RECENT) && (m_location != Location::TRASH) &&
                            context().services.files().is_available() && !is_protected(m_path);
    item(LV_SYMBOL_DIRECTORY "  New folder", MENU_NEW_FOLDER, can_create);
    item(m_show_hidden ? (LV_SYMBOL_OK "  Show hidden files") : ("      Show hidden files"), MENU_HIDDEN, true);
    item(LV_SYMBOL_REFRESH "  Refresh", MENU_REFRESH, true);
    if (m_location == Location::TRASH) {
        item(LV_SYMBOL_TRASH "  Empty trash", MENU_EMPTY_TRASH, m_count > 0U);
    }
}

void FilesApp::close_menu()
{
    if (m_p_menu != nullptr) {
        lv_obj_delete_async(m_p_menu); // Có thể đang ở trong callback của chính menu.
        m_p_menu = nullptr;
    }
}

void FilesApp::open_prompt(PromptKind kind, const char* p_title, const char* p_initial)
{
    close_prompt();
    m_prompt_kind = kind;
    m_p_prompt = overlay(LV_OPA_60);
    lv_obj_t* const p_card = flex(m_p_prompt, LV_FLEX_FLOW_COLUMN, 12);
    lv_obj_set_size(p_card, fw::theme::px(PROMPT_W), LV_SIZE_CONTENT);
    lv_obj_align(p_card, LV_ALIGN_TOP_MID, 0, fw::theme::px(PROMPT_TOP));
    lv_obj_set_style_bg_color(p_card, lv_color_hex(fw::palette::SURFACE_HIGH), 0);
    lv_obj_set_style_bg_opa(p_card, LV_OPA_COVER, 0);
    lv_obj_set_style_radius(p_card, fw::theme::px(fw::alert::RADIUS), 0);
    lv_obj_set_style_pad_all(p_card, fw::theme::px(fw::alert::BODY_PAD_HOR), 0);
    lv_obj_set_clickable(p_card, true);
    lv_obj_t* const p_title_label = text(p_card, p_title, COLOR_TEXT, fw::theme::font_medium());
    lv_obj_set_width(p_title_label, LV_PCT(100));
    lv_obj_set_style_text_align(p_title_label, LV_TEXT_ALIGN_CENTER, 0);

    m_p_prompt_field = lv_textarea_create(p_card);
    fw::keyboard::style_field(m_p_prompt_field);
    lv_obj_set_width(m_p_prompt_field, LV_PCT(100));
    lv_textarea_set_one_line(m_p_prompt_field, true);
    lv_textarea_set_max_length(m_p_prompt_field, static_cast<uint32_t>(fw::FILE_NAME_MAX_LEN));
    lv_textarea_set_text(m_p_prompt_field, p_initial);
    lv_obj_add_state(m_p_prompt_field, LV_STATE_FOCUSED);

    lv_obj_t* const p_buttons = flex(p_card, LV_FLEX_FLOW_ROW, 12);
    lv_obj_set_width(p_buttons, LV_PCT(100));
    lv_obj_set_flex_align(p_buttons, LV_FLEX_ALIGN_END, LV_FLEX_ALIGN_CENTER, LV_FLEX_ALIGN_CENTER);
    (void)flat_button(p_buttons, "Cancel", &FilesApp::on_prompt_button, this, 0U);
    lv_obj_t* const p_ok = flat_button(p_buttons, "OK", &FilesApp::on_prompt_button, this, 1U);
    lv_obj_set_style_bg_color(p_ok, lv_color_hex(fw::palette::ACCENT), 0);

    lv_obj_t* const p_kb = lv_keyboard_create(m_p_prompt);
    lv_obj_set_size(p_kb, LV_PCT(100), fw::theme::px(fw::keyboard::HEIGHT));
    lv_obj_align(p_kb, LV_ALIGN_BOTTOM_MID, 0, 0);
    fw::keyboard::style(p_kb);
    lv_keyboard_set_textarea(p_kb, m_p_prompt_field);
    lv_obj_add_event_cb(p_kb, &FilesApp::on_prompt_key, LV_EVENT_READY, this);
    lv_obj_add_event_cb(p_kb, &FilesApp::on_prompt_key, LV_EVENT_CANCEL, this);
}

void FilesApp::close_prompt()
{
    if (m_p_prompt != nullptr) {
        lv_obj_delete_async(m_p_prompt); // Có thể đang ở trong callback của bàn phím.
        m_p_prompt = nullptr;
        m_p_prompt_field = nullptr;
    }
    m_prompt_kind = PromptKind::NONE;
}

void FilesApp::confirm_prompt()
{
    if (m_p_prompt_field == nullptr) {
        return;
    }
    char name[fw::FILE_NAME_MAX_LEN + 1U];
    (void)common::copy_string(name, sizeof(name), lv_textarea_get_text(m_p_prompt_field));
    const PromptKind kind = m_prompt_kind;
    close_prompt();
    if (!fw::FileService::is_valid_name(name)) {
        set_status("Invalid name (avoid / \\ : * ? \" < > |)");
        render_footer();
        return;
    }
    fw::FileService& files = context().services.files();
    char target[fw::FILE_PATH_BUF_LEN];
    if (kind == PromptKind::NEW_FOLDER) {
        const bool is_ok =
            fw::FileService::join(target, sizeof(target), m_path, name) && common::is_ok(files.make_dir(target));
        set_status(is_ok ? "Folder created" : "Cannot create folder (name already used?)");
    } else if (kind == PromptKind::RENAME) {
        char parent[fw::FILE_PATH_BUF_LEN];
        (void)common::copy_string(parent, sizeof(parent), m_target);
        char* const p_slash = std::strrchr(parent, '/');
        if (p_slash != nullptr) {
            *p_slash = '\0';
        } else {
            parent[0] = '\0';
        }
        const bool is_ok = fw::FileService::join(target, sizeof(target), parent, name) &&
                           common::is_ok(files.rename(m_target, target));
        set_status(is_ok ? "Renamed" : "Cannot rename (name already used?)");
    } else {
        return;
    }
    reload();
}

void FilesApp::close_viewer()
{
    if (m_p_viewer != nullptr) {
        lv_obj_delete_async(m_p_viewer);
        m_p_viewer = nullptr;
    }
}

void FilesApp::close_dialog()
{
    if (m_p_dialog != nullptr) {
        lv_msgbox_close_async(m_p_dialog);
        m_p_dialog = nullptr;
    }
}

// ============================================================================
// Callback LVGL
// ============================================================================
void FilesApp::on_sidebar(lv_event_t* p_event)
{
    auto* const p_self = static_cast<FilesApp*>(lv_event_get_user_data(p_event));
    const auto index = reinterpret_cast<uintptr_t>(lv_obj_get_user_data(lv_event_get_current_target_obj(p_event)));
    p_self->open_location(static_cast<Location>(index));
}

void FilesApp::on_item_clicked(lv_event_t* p_event)
{
    auto* const p_self = static_cast<FilesApp*>(lv_event_get_user_data(p_event));
    lv_obj_t* const p_item = lv_event_get_current_target_obj(p_event);
    if (p_self->m_p_long_pressed == p_item) {
        p_self->m_p_long_pressed = nullptr; // CLICKED đi kèm lần nhấn giữ vừa rồi.
        return;
    }
    const auto entry = static_cast<size_t>(reinterpret_cast<uintptr_t>(lv_obj_get_user_data(p_item)));
    if (p_self->m_selected_count > 0U) {
        p_self->toggle_select(entry); // Đang ở chế độ chọn: chạm = chọn / bỏ chọn.
    } else {
        p_self->open_entry(entry);
    }
}

void FilesApp::on_item_long_pressed(lv_event_t* p_event)
{
    auto* const p_self = static_cast<FilesApp*>(lv_event_get_user_data(p_event));
    lv_obj_t* const p_item = lv_event_get_current_target_obj(p_event);
    if ((p_self->m_location == Location::RECENT) || (p_self->m_queue_kind != QueueKind::NONE)) {
        return; // Danh sách gần đây chỉ để mở nhanh.
    }
    p_self->m_p_long_pressed = p_item;
    p_self->toggle_select(static_cast<size_t>(reinterpret_cast<uintptr_t>(lv_obj_get_user_data(p_item))));
}

void FilesApp::on_crumb(lv_event_t* p_event)
{
    auto* const p_self = static_cast<FilesApp*>(lv_event_get_user_data(p_event));
    const auto prefix = static_cast<size_t>(
        reinterpret_cast<uintptr_t>(lv_obj_get_user_data(lv_event_get_current_target_obj(p_event))));
    char path[fw::FILE_PATH_BUF_LEN];
    (void)common::copy_string(path, sizeof(path), p_self->m_path);
    if (prefix < sizeof(path)) {
        path[prefix] = '\0';
    }
    p_self->navigate(path, true);
}

void FilesApp::on_history(lv_event_t* p_event)
{
    auto* const p_self = static_cast<FilesApp*>(lv_event_get_user_data(p_event));
    p_self->history_step(lv_obj_get_user_data(lv_event_get_current_target_obj(p_event)) != nullptr); // 1 = ›
}

void FilesApp::on_header(lv_event_t* p_event)
{
    auto* const p_self = static_cast<FilesApp*>(lv_event_get_user_data(p_event));
    const auto key = static_cast<SortKey>(
        reinterpret_cast<uintptr_t>(lv_obj_get_user_data(lv_event_get_current_target_obj(p_event))));
    p_self->m_sort_desc = (p_self->m_sort == key) ? !p_self->m_sort_desc : false;
    p_self->m_sort = key;
    p_self->clear_selection();
    p_self->sort_entries();
    p_self->m_page = 0U;
    p_self->render();
}

void FilesApp::on_view_toggle(lv_event_t* p_event)
{
    auto* const p_self = static_cast<FilesApp*>(lv_event_get_user_data(p_event));
    p_self->m_is_grid = !p_self->m_is_grid;
    p_self->render();
    p_self->update_selection_style();
}

void FilesApp::on_menu(lv_event_t* p_event)
{
    auto* const p_self = static_cast<FilesApp*>(lv_event_get_user_data(p_event));
    if ((p_self->m_p_menu != nullptr) && (lv_event_get_current_target_obj(p_event) == p_self->m_p_menu)) {
        p_self->close_menu(); // Chạm ra ngoài thẻ menu.
        return;
    }
    p_self->open_menu();
}

void FilesApp::on_menu_item(lv_event_t* p_event)
{
    auto* const p_self = static_cast<FilesApp*>(lv_event_get_user_data(p_event));
    const auto tag = reinterpret_cast<uintptr_t>(lv_obj_get_user_data(lv_event_get_current_target_obj(p_event)));
    p_self->close_menu();
    if (tag == MENU_NEW_FOLDER) {
        p_self->open_prompt(PromptKind::NEW_FOLDER, "New folder", "New Folder");
    } else if (tag == MENU_HIDDEN) {
        p_self->m_show_hidden = !p_self->m_show_hidden;
        p_self->reload();
    } else if (tag == MENU_REFRESH) {
        p_self->reload();
        p_self->render_storage();
    } else if (tag == MENU_EMPTY_TRASH) {
        p_self->close_dialog();
        p_self->m_p_dialog = fw::alert::show(
            "Empty Trash?", "All items in the Trash will be permanently deleted.",
            fw::alert::Action{"Cancel", fw::palette::ACCENT, &FilesApp::on_dialog_cancel},
            fw::alert::Action{"Empty", fw::palette::DESTRUCTIVE, &FilesApp::on_empty_confirm}, p_self);
    } else {
        // Không có mục khác.
    }
}

void FilesApp::on_action(lv_event_t* p_event)
{
    auto* const p_self = static_cast<FilesApp*>(lv_event_get_user_data(p_event));
    const auto tag = reinterpret_cast<uintptr_t>(lv_obj_get_user_data(lv_event_get_current_target_obj(p_event)));
    switch (tag) {
    case ACT_COPY:
        p_self->copy_selection(ClipOp::COPY);
        break;
    case ACT_CUT:
        p_self->copy_selection(ClipOp::CUT);
        break;
    case ACT_RENAME:
        for (size_t index = 0U; index < p_self->m_count; ++index) {
            if (p_self->m_selected[index] && p_self->entry_path(index, p_self->m_target, sizeof(p_self->m_target))) {
                if (p_self->is_protected(p_self->m_target)) {
                    p_self->set_status("This item is managed by the system");
                    p_self->render_footer();
                    return;
                }
                p_self->open_prompt(PromptKind::RENAME, "Rename", p_self->m_entries[index].name);
                break;
            }
        }
        break;
    case ACT_DELETE:
        p_self->trash_selection();
        break;
    case ACT_RESTORE:
        p_self->restore_selection();
        break;
    case ACT_FOREVER: {
        char title[48];
        (void)std::snprintf(title, sizeof(title), "Delete %u item(s) forever?",
                            static_cast<unsigned>(p_self->m_selected_count));
        p_self->close_dialog();
        p_self->m_p_dialog =
            fw::alert::show(title, "This cannot be undone.",
                            fw::alert::Action{"Cancel", fw::palette::ACCENT, &FilesApp::on_dialog_cancel},
                            fw::alert::Action{"Delete", fw::palette::DESTRUCTIVE, &FilesApp::on_forever_confirm},
                            p_self);
        break;
    }
    case ACT_SELECT_ALL:
        for (size_t index = 0U; index < p_self->m_count; ++index) {
            p_self->m_selected[index] = true;
        }
        p_self->m_selected_count = p_self->m_count;
        p_self->update_selection_style();
        break;
    case ACT_CANCEL_SEL:
        p_self->clear_selection();
        break;
    case ACT_CANCEL_JOB:
        p_self->context().services.files().cancel_job();
        p_self->set_status("Cancelled");
        p_self->finish_queue(false);
        break;
    default:
        break;
    }
}

void FilesApp::on_page(lv_event_t* p_event)
{
    auto* const p_self = static_cast<FilesApp*>(lv_event_get_user_data(p_event));
    const bool is_next = lv_obj_get_user_data(lv_event_get_current_target_obj(p_event)) != nullptr;
    const size_t pages = (p_self->m_count + PAGE_SIZE - 1U) / PAGE_SIZE;
    if (is_next && ((p_self->m_page + 1U) < pages)) {
        ++p_self->m_page;
    } else if (!is_next && (p_self->m_page > 0U)) {
        --p_self->m_page;
    } else {
        return;
    }
    p_self->render();
    p_self->update_selection_style();
    lv_obj_scroll_to_y(p_self->m_p_list, 0, LV_ANIM_OFF);
}

void FilesApp::on_paste(lv_event_t* p_event)
{
    static_cast<FilesApp*>(lv_event_get_user_data(p_event))->paste();
}

void FilesApp::on_prompt_key(lv_event_t* p_event)
{
    auto* const p_self = static_cast<FilesApp*>(lv_event_get_user_data(p_event));
    if (lv_event_get_code(p_event) == LV_EVENT_READY) {
        p_self->confirm_prompt();
    } else {
        p_self->close_prompt();
    }
}

void FilesApp::on_prompt_button(lv_event_t* p_event)
{
    auto* const p_self = static_cast<FilesApp*>(lv_event_get_user_data(p_event));
    if (lv_obj_get_user_data(lv_event_get_current_target_obj(p_event)) != nullptr) {
        p_self->confirm_prompt();
    } else {
        p_self->close_prompt();
    }
}

void FilesApp::on_viewer_close(lv_event_t* p_event)
{
    static_cast<FilesApp*>(lv_event_get_user_data(p_event))->close_viewer();
}

void FilesApp::on_dialog_cancel(lv_event_t* p_event)
{
    static_cast<FilesApp*>(lv_event_get_user_data(p_event))->close_dialog();
}

void FilesApp::on_install_confirm(lv_event_t* p_event)
{
    auto* const p_self = static_cast<FilesApp*>(lv_event_get_user_data(p_event));
    p_self->close_dialog();
    // Gói phải nằm trong install/: chép vào đó trước (ghi đè bản cũ cùng tên), rồi runtime cài như nút thẻ SD.
    char staged[fw::FILE_PATH_BUF_LEN];
    if (!fw::FileService::join(staged, sizeof(staged), DIR_INSTALL, fw::FileService::base_name(p_self->m_target))) {
        return;
    }
    fw::FileService& files = p_self->context().services.files();
    if (std::strcmp(staged, p_self->m_target) == 0) {
        p_self->m_queue_kind = QueueKind::INSTALL;
        p_self->finish_queue(true);
        return;
    }
    (void)files.make_dir(DIR_INSTALL);
    if (files.exists(staged)) {
        (void)files.start_delete(staged); // Một file: xong ngay.
    }
    p_self->m_queue_count = 1U;
    (void)common::copy_string(p_self->m_queue[0], sizeof(p_self->m_queue[0]), p_self->m_target);
    (void)common::copy_string(p_self->m_queue_dir, sizeof(p_self->m_queue_dir), DIR_INSTALL);
    p_self->start_queue(QueueKind::INSTALL);
}

void FilesApp::on_forever_confirm(lv_event_t* p_event)
{
    auto* const p_self = static_cast<FilesApp*>(lv_event_get_user_data(p_event));
    p_self->close_dialog();
    p_self->erase_selection_forever();
}

void FilesApp::on_empty_confirm(lv_event_t* p_event)
{
    auto* const p_self = static_cast<FilesApp*>(lv_event_get_user_data(p_event));
    p_self->close_dialog();
    p_self->empty_trash();
}

void FilesApp::on_job_timer(lv_timer_t* p_timer)
{
    auto* const p_self = static_cast<FilesApp*>(lv_timer_get_user_data(p_timer));
    fw::FileService& files = p_self->context().services.files();
    const fw::FileJobState state = files.step(JOB_BUDGET_MS);
    if (state == fw::FileJobState::RUNNING) {
        p_self->render_footer();
        return;
    }
    if (state != fw::FileJobState::IDLE) { // Xong / lỗi một mục: cộng dồn rồi sang mục kế.
        p_self->m_queue_done += files.job_items();
        if (state == fw::FileJobState::FAILED) {
            ++p_self->m_queue_failed;
        }
    }
    while (p_self->m_queue_next < p_self->m_queue_count) {
        const char* const p_src = p_self->m_queue[p_self->m_queue_next++];
        common::Status status = common::Status::ERR_PARAM;
        if (p_self->m_queue_kind == QueueKind::DELETE) {
            status = files.start_delete(p_src);
        } else {
            char target[fw::FILE_PATH_BUF_LEN];
            if (p_self->unique_path(p_self->m_queue_dir, fw::FileService::base_name(p_src), target, sizeof(target))) {
                status = files.start_copy(p_src, target);
            }
        }
        if (common::is_ok(status)) {
            p_self->render_footer();
            return; // Bước tiếp theo ở lần timer sau.
        }
        ++p_self->m_queue_failed;
        LOGW(TAG, "queue item %s failed (status=%u)", p_src, static_cast<unsigned>(status));
    }
    p_self->finish_queue(p_self->m_queue_failed == 0U);
}

} // namespace apps
