/**
 * @file    app_store_app.cpp
 * @brief   Hiện thực app quản lý ứng dụng cài thêm.
 */
#include "app_store_app.hpp"

#include "framework/system_ui.hpp"

#include <cstdio>
#include <cstring>

#include "common/string_utils.hpp"
#include "framework/list_view.hpp"
#include "framework/theme.hpp"
#include "runtime/sdk_version.hpp"
#include "ui_helpers.hpp"

namespace apps {

namespace {

constexpr fw::AppInfo INFO{"apps", "Apps", LV_SYMBOL_DOWNLOAD, 0xE3B341U, true, false};
constexpr size_t TEXT_LEN = 96U;
constexpr int32_t INSTALL_BUTTON_SIZE = 36;
constexpr size_t PAGE_SIZE = 20U; /**< Mỗi dòng ~0.75 KiB heap LVGL: 128 dòng cùng lúc sẽ làm cạn heap. */
constexpr uint32_t POLL_MS = 200U;
constexpr size_t PERCENT = 100U;

bool is_busy(runtime::StoreState state)
{
    return (state == runtime::StoreState::LOADING_INDEX) || (state == runtime::StoreState::DOWNLOADING);
}

void set_enabled(lv_obj_t* p_obj, bool is_enabled)
{
    if (is_enabled) {
        lv_obj_remove_state(p_obj, LV_STATE_DISABLED);
    } else {
        lv_obj_add_state(p_obj, LV_STATE_DISABLED);
    }
}

} // namespace

AppStoreApp::AppStoreApp()
    : fw::App(INFO), m_p_status(nullptr), m_p_install_btn(nullptr), m_p_mode_btn(nullptr), m_p_refresh_btn(nullptr),
      m_p_timer(nullptr), m_store_mode(false), m_store_loaded(false), m_last_state(runtime::StoreState::IDLE),
      m_p_prev_btn(nullptr), m_p_next_btn(nullptr),
      m_page(0U), m_p_list(nullptr), m_p_dialog(nullptr), m_selected_id{}
{
}

void AppStoreApp::on_create(lv_obj_t* p_root)
{
    ui::column_layout(p_root);

    lv_obj_t* const p_header = lv_obj_create(p_root);
    lv_obj_remove_style_all(p_header);
    lv_obj_set_size(p_header, LV_PCT(100), LV_SIZE_CONTENT);
    lv_obj_set_flex_flow(p_header, LV_FLEX_FLOW_ROW);
    lv_obj_set_flex_align(p_header, LV_FLEX_ALIGN_START, LV_FLEX_ALIGN_CENTER, LV_FLEX_ALIGN_CENTER);
    lv_obj_set_style_pad_column(p_header, fw::theme::pad(), 0);

    m_p_status = ui::label(p_header, "", fw::theme::font_small(), fw::theme::COLOR_TEXT_MUTED);
    lv_obj_set_flex_grow(m_p_status, 1);
    lv_label_set_long_mode(m_p_status, LV_LABEL_LONG_DOT);
    const int32_t button_size = fw::theme::px(INSTALL_BUTTON_SIZE);
    m_p_prev_btn = ui::button(p_header, LV_SYMBOL_LEFT, &AppStoreApp::on_prev_clicked, this);
    lv_obj_set_size(m_p_prev_btn, button_size, button_size);
    m_p_next_btn = ui::button(p_header, LV_SYMBOL_RIGHT, &AppStoreApp::on_next_clicked, this);
    lv_obj_set_size(m_p_next_btn, button_size, button_size);
    m_p_install_btn = ui::button(p_header, LV_SYMBOL_SD_CARD, &AppStoreApp::on_install_clicked, this);
    lv_obj_set_size(m_p_install_btn, button_size, button_size);
    m_p_refresh_btn = ui::button(p_header, LV_SYMBOL_REFRESH, &AppStoreApp::on_refresh_clicked, this);
    lv_obj_set_size(m_p_refresh_btn, button_size, button_size);
    m_p_mode_btn = ui::button(p_header, LV_SYMBOL_DOWNLOAD " Store", &AppStoreApp::on_mode_clicked, this);
    lv_obj_set_height(m_p_mode_btn, button_size);

    m_p_list = fw::list_view::create(p_root);
}

void AppStoreApp::on_resume()
{
    rebuild();
    if (m_p_timer == nullptr) {
        m_p_timer = lv_timer_create(&AppStoreApp::on_timer, POLL_MS, this);
    }
}

void AppStoreApp::on_pause()
{
    close_dialog();
    if (m_p_timer != nullptr) { // Việc tải vẫn chạy ở nền; kết quả được xử lý lần mở sau.
        lv_timer_delete(m_p_timer);
        m_p_timer = nullptr;
    }
}

bool AppStoreApp::on_back()
{
    if (m_p_dialog != nullptr) {
        close_dialog();
        return true;
    }
    return false;
}

void AppStoreApp::rebuild()
{
    lv_obj_clean(m_p_list);
    lv_label_set_text(lv_obj_get_child(m_p_mode_btn, 0), m_store_mode ? LV_SYMBOL_LIST " Installed"
                                                                       : LV_SYMBOL_DOWNLOAD " Store");
    lv_obj_set_hidden(m_p_install_btn, m_store_mode);
    lv_obj_set_hidden(m_p_refresh_btn, !m_store_mode);
    if (m_store_mode) {
        rebuild_store();
        return;
    }
    if (!runtime::is_available()) {
        set_status(LV_SYMBOL_WARNING " No SD card", fw::theme::COLOR_DANGER);
        set_enabled(m_p_install_btn, false);
        set_enabled(m_p_prev_btn, false);
        set_enabled(m_p_next_btn, false);
        return;
    }
    set_enabled(m_p_install_btn, true);

    const size_t count = runtime::package_count();
    const size_t page_count = (count == 0U) ? 1U : ((count + PAGE_SIZE - 1U) / PAGE_SIZE);
    if (m_page >= page_count) {
        m_page = page_count - 1U; // Gỡ app làm mất trang cuối.
    }
    set_enabled(m_p_prev_btn, m_page > 0U);
    set_enabled(m_p_next_btn, (m_page + 1U) < page_count);
    const size_t first = m_page * PAGE_SIZE;
    const size_t last = ((first + PAGE_SIZE) < count) ? (first + PAGE_SIZE) : count;
    for (size_t index = first; index < last; ++index) {
        runtime::PackageView view{};
        if (runtime::package_at(index, view)) {
            char text[TEXT_LEN] = {};
            (void)std::snprintf(text, sizeof(text), "%s  v%s", view.p_name, view.p_version);
            lv_obj_t* const p_item = fw::list_view::add(m_p_list, LV_SYMBOL_FILE, text);
            lv_obj_set_user_data(p_item, reinterpret_cast<void*>(static_cast<uintptr_t>(index)));
            lv_obj_add_event_cb(p_item, &AppStoreApp::on_package_clicked, LV_EVENT_CLICKED, this);
        }
    }

    char status[TEXT_LEN] = {};
    if (runtime::last_error()[0] != '\0') {
        (void)common::copy_string(status, sizeof(status), runtime::last_error());
        set_status(status, fw::theme::COLOR_DANGER);
    } else {
        (void)std::snprintf(status, sizeof(status), "%u/%u installed  -  page %u/%u", static_cast<unsigned>(count),
                            static_cast<unsigned>(runtime::MAX_PACKAGES), static_cast<unsigned>(m_page + 1U),
                            static_cast<unsigned>(page_count));
        set_status(status, fw::theme::COLOR_TEXT_MUTED);
    }
}

void AppStoreApp::set_status(const char* p_text, uint32_t color_hex)
{
    lv_label_set_text(m_p_status, p_text);
    lv_obj_set_style_text_color(m_p_status, lv_color_hex(color_hex), 0);
}

void AppStoreApp::confirm_uninstall(size_t index)
{
    runtime::PackageView view{};
    if ((m_p_dialog != nullptr) || !runtime::package_at(index, view)) {
        return;
    }
    (void)common::copy_string(m_selected_id, sizeof(m_selected_id), view.p_id);
    char title[TEXT_LEN] = {};
    (void)std::snprintf(title, sizeof(title), "Uninstall \"%s\"?", view.p_name);
    m_p_dialog = fw::alert::show(title, "The app and all of its data will be removed from this device.",
                             fw::alert::Action{"Cancel", fw::alert::COLOR_CANCEL, &AppStoreApp::on_dialog_cancel},
                             fw::alert::Action{"Uninstall", fw::alert::COLOR_DESTRUCTIVE, &AppStoreApp::on_uninstall_confirmed},
                             this);
}

void AppStoreApp::close_dialog()
{
    if (m_p_dialog != nullptr) {
        lv_msgbox_close_async(m_p_dialog);
        m_p_dialog = nullptr;
    }
}

void AppStoreApp::on_install_clicked(lv_event_t* p_event)
{
    auto* const p_self = static_cast<AppStoreApp*>(lv_event_get_user_data(p_event));
    size_t installed = 0U;
    const common::Status status = runtime::install_pending(installed);
    p_self->rebuild();
    if (common::is_ok(status)) {
        char text[TEXT_LEN] = {};
        if (installed > 0U) {
            (void)std::snprintf(text, sizeof(text), LV_SYMBOL_OK " Installed %u app(s)",
                                static_cast<unsigned>(installed));
        } else {
            (void)common::copy_string(text, sizeof(text), "No .gapp in /install");
        }
        p_self->set_status(text, fw::theme::COLOR_SECONDARY);
    }
}

void AppStoreApp::change_page(int32_t delta)
{
    if ((delta < 0) && (m_page > 0U)) {
        --m_page;
    } else if (delta > 0) {
        ++m_page; // rebuild() chặn lại nếu vượt trang cuối.
    } else {
        return;
    }
    rebuild();
    lv_obj_scroll_to_y(m_p_list, 0, LV_ANIM_OFF);
}

void AppStoreApp::on_prev_clicked(lv_event_t* p_event)
{
    static_cast<AppStoreApp*>(lv_event_get_user_data(p_event))->change_page(-1);
}

void AppStoreApp::on_next_clicked(lv_event_t* p_event)
{
    static_cast<AppStoreApp*>(lv_event_get_user_data(p_event))->change_page(1);
}

void AppStoreApp::on_package_clicked(lv_event_t* p_event)
{
    auto* const p_self = static_cast<AppStoreApp*>(lv_event_get_user_data(p_event));
    void* const p_data = lv_obj_get_user_data(lv_event_get_current_target_obj(p_event));
    p_self->confirm_uninstall(static_cast<size_t>(reinterpret_cast<uintptr_t>(p_data)));
}

void AppStoreApp::on_uninstall_confirmed(lv_event_t* p_event)
{
    auto* const p_self = static_cast<AppStoreApp*>(lv_event_get_user_data(p_event));
    p_self->close_dialog();
    (void)runtime::uninstall(p_self->m_selected_id);
    p_self->rebuild();
}

// ============================================================================
// Kho trực tuyến
// ============================================================================
void AppStoreApp::rebuild_store()
{
    const size_t count = runtime::store_count();
    const size_t page_count = (count == 0U) ? 1U : ((count + PAGE_SIZE - 1U) / PAGE_SIZE);
    m_page = (m_page >= page_count) ? (page_count - 1U) : m_page;
    set_enabled(m_p_prev_btn, m_page > 0U);
    set_enabled(m_p_next_btn, (m_page + 1U) < page_count);
    set_enabled(m_p_refresh_btn, !is_busy(m_last_state));
    const size_t first = m_page * PAGE_SIZE;
    const size_t last = ((first + PAGE_SIZE) < count) ? (first + PAGE_SIZE) : count;
    for (size_t index = first; index < last; ++index) {
        const runtime::StoreApp& app = runtime::store_app(index);
        runtime::PackageDetails installed{};
        const bool has = runtime::package_details(app.id, installed);
        char text[TEXT_LEN] = {};
        const char* p_symbol = LV_SYMBOL_DOWNLOAD;
        if (app.sdk > runtime::SDK_VERSION) {
            (void)std::snprintf(text, sizeof(text), "%s  v%s  -  needs newer firmware", app.name, app.version);
            p_symbol = LV_SYMBOL_WARNING;
        } else if (!has) {
            (void)std::snprintf(text, sizeof(text), "%s  v%s  -  Install  (%u KB)", app.name, app.version,
                                static_cast<unsigned>((app.size + 1023U) / 1024U));
        } else if (std::strcmp(installed.p_version, app.version) != 0) {
            (void)std::snprintf(text, sizeof(text), "%s  v%s -> v%s  -  Update", app.name, installed.p_version,
                                app.version);
            p_symbol = LV_SYMBOL_REFRESH;
        } else {
            (void)std::snprintf(text, sizeof(text), "%s  v%s  -  Installed", app.name, app.version);
            p_symbol = LV_SYMBOL_OK;
        }
        lv_obj_t* const p_item = fw::list_view::add(m_p_list, p_symbol, text);
        lv_obj_set_user_data(p_item, reinterpret_cast<void*>(static_cast<uintptr_t>(index)));
        lv_obj_add_event_cb(p_item, &AppStoreApp::on_store_item_clicked, LV_EVENT_CLICKED, this);
    }
    show_store_message();
}

void AppStoreApp::show_store_message()
{
    char text[TEXT_LEN] = {};
    uint32_t color = fw::theme::COLOR_TEXT_MUTED;
    if (m_last_state == runtime::StoreState::LOADING_INDEX) {
        (void)common::copy_string(text, sizeof(text), "Loading store...");
    } else if (m_last_state == runtime::StoreState::DOWNLOADING) {
        size_t received = 0U;
        size_t total = 0U;
        runtime::store_progress(received, total);
        (void)std::snprintf(text, sizeof(text), LV_SYMBOL_DOWNLOAD " Downloading  %u%%  (%u / %u KB)",
                            static_cast<unsigned>((total > 0U) ? ((received * PERCENT) / total) : 0U),
                            static_cast<unsigned>(received / 1024U), static_cast<unsigned>(total / 1024U));
    } else if (m_last_state == runtime::StoreState::FAILED) {
        (void)std::snprintf(text, sizeof(text), LV_SYMBOL_WARNING " %s", runtime::store_message());
        color = fw::theme::COLOR_DANGER;
    } else if (runtime::store_message()[0] != '\0') {
        (void)std::snprintf(text, sizeof(text), "%s  -  %s", runtime::store_message(), runtime::store_url());
        color = fw::theme::COLOR_SECONDARY;
    } else {
        (void)std::snprintf(text, sizeof(text), "Store: %s", runtime::store_url());
    }
    set_status(text, color);
}

void AppStoreApp::store_item_clicked(size_t index)
{
    if (is_busy(m_last_state)) {
        return;
    }
    const runtime::StoreApp& app = runtime::store_app(index);
    runtime::PackageDetails installed{};
    if (runtime::package_details(app.id, installed) && (std::strcmp(installed.p_version, app.version) == 0)) {
        set_status(LV_SYMBOL_OK " Already up to date", fw::theme::COLOR_SECONDARY);
        return;
    }
    if (common::is_ok(runtime::store_install(index))) {
        m_last_state = runtime::StoreState::DOWNLOADING;
        set_enabled(m_p_refresh_btn, false);
    }
    show_store_message();
}

void AppStoreApp::on_mode_clicked(lv_event_t* p_event)
{
    auto* const p_self = static_cast<AppStoreApp*>(lv_event_get_user_data(p_event));
    p_self->m_store_mode = !p_self->m_store_mode;
    p_self->m_page = 0U;
    if (p_self->m_store_mode && !p_self->m_store_loaded && common::is_ok(runtime::store_refresh())) {
        p_self->m_last_state = runtime::StoreState::LOADING_INDEX;
    }
    p_self->rebuild();
}

void AppStoreApp::on_refresh_clicked(lv_event_t* p_event)
{
    auto* const p_self = static_cast<AppStoreApp*>(lv_event_get_user_data(p_event));
    if (common::is_ok(runtime::store_refresh())) {
        p_self->m_last_state = runtime::StoreState::LOADING_INDEX;
    }
    p_self->rebuild();
}

void AppStoreApp::on_store_item_clicked(lv_event_t* p_event)
{
    auto* const p_self = static_cast<AppStoreApp*>(lv_event_get_user_data(p_event));
    void* const p_data = lv_obj_get_user_data(lv_event_get_current_target_obj(p_event));
    p_self->store_item_clicked(static_cast<size_t>(reinterpret_cast<uintptr_t>(p_data)));
}

void AppStoreApp::on_timer(lv_timer_t* p_timer)
{
    auto* const p_self = static_cast<AppStoreApp*>(lv_timer_get_user_data(p_timer));
    const runtime::StoreState state = runtime::store_poll();
    const bool changed = (state != p_self->m_last_state);
    const runtime::StoreState previous = p_self->m_last_state;
    p_self->m_last_state = state;
    if (changed && (previous == runtime::StoreState::LOADING_INDEX) && (state == runtime::StoreState::DONE)) {
        p_self->m_store_loaded = true;
    }
    if (!p_self->m_store_mode) {
        return;
    }
    if (changed) {
        p_self->rebuild(); // Xong / lỗi: dựng lại danh sách (trạng thái Install / Update / Installed).
    } else if (is_busy(state)) {
        p_self->show_store_message(); // Tiến độ.
    } else {
        // Không đổi.
    }
}

void AppStoreApp::on_dialog_cancel(lv_event_t* p_event)
{
    static_cast<AppStoreApp*>(lv_event_get_user_data(p_event))->close_dialog();
}

} // namespace apps
