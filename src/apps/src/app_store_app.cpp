/**
 * @file    app_store_app.cpp
 * @brief   Hiện thực app quản lý ứng dụng cài thêm.
 */
#include "app_store_app.hpp"

#include <cstdio>

#include "common/string_utils.hpp"
#include "framework/list_view.hpp"
#include "framework/theme.hpp"
#include "ui_helpers.hpp"

namespace apps {

namespace {

constexpr fw::AppInfo INFO{"apps", "Apps", LV_SYMBOL_DOWNLOAD, 0xE3B341U, true, false};
constexpr size_t TEXT_LEN = 96U;
constexpr int32_t INSTALL_BUTTON_SIZE = 36;
constexpr size_t PAGE_SIZE = 20U; /**< Mỗi dòng ~0.75 KiB heap LVGL: 128 dòng cùng lúc sẽ làm cạn heap. */

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
    : fw::App(INFO), m_p_status(nullptr), m_p_install_btn(nullptr), m_p_prev_btn(nullptr), m_p_next_btn(nullptr),
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

    m_p_list = fw::list_view::create(p_root);
}

void AppStoreApp::on_resume()
{
    rebuild();
}

void AppStoreApp::on_pause()
{
    close_dialog();
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
    (void)std::snprintf(title, sizeof(title), "Uninstall %s?", view.p_name);

    m_p_dialog = lv_msgbox_create(nullptr);
    lv_msgbox_add_title(m_p_dialog, title);
    lv_obj_t* const p_ok = lv_msgbox_add_footer_button(m_p_dialog, "Uninstall");
    lv_obj_add_event_cb(p_ok, &AppStoreApp::on_uninstall_confirmed, LV_EVENT_CLICKED, this);
    lv_obj_t* const p_cancel = lv_msgbox_add_footer_button(m_p_dialog, "Cancel");
    lv_obj_add_event_cb(p_cancel, &AppStoreApp::on_dialog_cancel, LV_EVENT_CLICKED, this);
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

void AppStoreApp::on_dialog_cancel(lv_event_t* p_event)
{
    static_cast<AppStoreApp*>(lv_event_get_user_data(p_event))->close_dialog();
}

} // namespace apps
