/**
 * @file    app_manager.cpp
 * @brief   Hiện thực registry + back-stack ứng dụng.
 */
#include "framework/app_manager.hpp"

#include <cstring>

#include "common/event.hpp"
#include "common/log.hpp"
#include "common/trace.hpp"
#include "common/string_utils.hpp"
#include "framework/services.hpp"
#include "storage_keys.hpp"
#include "osal/osal.hpp"

namespace fw {

namespace {
constexpr const char* TAG = "app";
constexpr uint32_t US_PER_MS = 1000U;
} // namespace

AppManager::AppManager(Services& services)
    : m_context{*this, services}, m_p_apps{}, m_app_count(0U), m_registry_generation(0U), m_p_home(nullptr),
      m_stack{}, m_stack_depth(0U),
      m_p_content(nullptr), m_p_listener(nullptr), m_disabled{}, m_disabled_count(0U)
{
}

common::Status AppManager::register_app(App& app)
{
    if ((app.info().p_id == nullptr) || (find(app.info().p_id) != nullptr)) {
        return common::Status::ERR_PARAM;
    }
    if (m_app_count >= config::MAX_APPS) {
        return common::Status::ERR_FULL;
    }
    app.m_p_context = &m_context;
    m_p_apps[m_app_count] = &app;
    m_app_count++;
    m_registry_generation++;
    return common::Status::OK;
}

common::Status AppManager::unregister_app(const char* p_id)
{
    App* const p_app = find(p_id);
    if ((p_app == nullptr) || (p_app == m_p_home)) {
        return common::Status::ERR_PARAM;
    }
    for (size_t index = 0U; index < m_stack_depth; ++index) {
        if (m_stack[index].p_app == p_app) {
            return common::Status::ERR_STATE;
        }
    }
    size_t slot = 0U;
    while (m_p_apps[slot] != p_app) {
        ++slot;
    }
    for (; (slot + 1U) < m_app_count; ++slot) {
        m_p_apps[slot] = m_p_apps[slot + 1U];
    }
    m_app_count--;
    m_p_apps[m_app_count] = nullptr;
    p_app->m_p_context = nullptr;
    m_registry_generation++;
    return common::Status::OK;
}

common::Status AppManager::set_home(const char* p_id)
{
    App* const p_app = find(p_id);
    if (p_app == nullptr) {
        LOGW(TAG, "launch: no app '%s'", (p_id != nullptr) ? p_id : "(null)");
        return common::Status::ERR_PARAM;
    }
    if (!is_enabled(p_id)) {
        LOGW(TAG, "launch: app '%s' is disabled", p_id);
        return common::Status::ERR_STATE;
    }
    m_p_home = p_app;
    return common::Status::OK;
}

common::Status AppManager::start(lv_obj_t* p_content, IAppListener* p_listener)
{
    if (p_content == nullptr) {
        return common::Status::ERR_PARAM;
    }
    if ((m_p_home == nullptr) || (m_p_content != nullptr)) {
        return common::Status::ERR_STATE;
    }
    m_p_content = p_content;
    m_p_listener = p_listener;
    load_disabled(); // Dịch vụ đã gắn thiết bị: đọc được NVS.
    return push(*m_p_home);
}

common::Status AppManager::launch(const char* p_id)
{
    App* const p_app = find(p_id);
    if (p_app == nullptr) {
        return common::Status::ERR_PARAM;
    }
    if (m_p_content == nullptr) {
        return common::Status::ERR_STATE;
    }

    // App đã có trong back-stack -> quay lại nó thay vì mở bản thứ hai.
    for (size_t index = 0U; index < m_stack_depth; ++index) {
        if (m_stack[index].p_app == p_app) {
            if (index + 1U == m_stack_depth) {
                return common::Status::OK;
            }
            while (m_stack_depth > (index + 1U)) {
                pop_top();
            }
            resume_top();
            return common::Status::OK;
        }
    }
    close_other_exclusive(*p_app);
    return push(*p_app);
}

void AppManager::close_other_exclusive(const App& app)
{
    if (!app.info().exclusive) {
        return;
    }
    bool removed_top = false;
    // Duyệt ngược để chỉ số phía dưới không bị dịch khi xóa.
    for (size_t index = m_stack_depth; index > 1U; --index) {
        const StackEntry& entry = m_stack[index - 1U];
        if ((entry.p_app != &app) && entry.p_app->info().exclusive) {
            removed_top = removed_top || (index == m_stack_depth);
            remove_at(index - 1U);
        }
    }
    if (removed_top) {
        // Đỉnh mới đang paused: resume để push() pause lại đúng vòng đời (không pause hai lần).
        resume_top();
    }
}

common::Status AppManager::close(const char* p_id)
{
    const App* const p_app = find(p_id);
    if ((p_app == nullptr) || (p_app == m_p_home)) {
        return common::Status::ERR_PARAM;
    }
    for (size_t index = 1U; index < m_stack_depth; ++index) {
        if (m_stack[index].p_app != p_app) {
            continue;
        }
        const bool is_top = (index + 1U) == m_stack_depth;
        remove_at(index);
        if (is_top) {
            resume_top();
        }
        return common::Status::OK;
    }
    return common::Status::ERR_STATE;
}

bool AppManager::is_essential(const char* p_id) const
{
    static const char* const ESSENTIAL[] = {"settings", "apps"}; // Cần để bật lại / quản lý app.
    if ((p_id == nullptr) || ((m_p_home != nullptr) && (std::strcmp(p_id, m_p_home->info().p_id) == 0))) {
        return true;
    }
    for (const char* const p_name : ESSENTIAL) {
        if (std::strcmp(p_id, p_name) == 0) {
            return true;
        }
    }
    return false;
}

bool AppManager::is_enabled(const char* p_id) const
{
    if (p_id == nullptr) {
        return false;
    }
    for (size_t index = 0U; index < m_disabled_count; ++index) {
        if (std::strcmp(m_disabled[index], p_id) == 0) {
            return false;
        }
    }
    return true;
}

common::Status AppManager::set_enabled(const char* p_id, bool is_enabled_now)
{
    if ((find(p_id) == nullptr) || is_essential(p_id)) {
        return common::Status::ERR_PARAM;
    }
    if (is_enabled_now == is_enabled(p_id)) {
        return common::Status::OK;
    }
    if (is_enabled_now) {
        for (size_t index = 0U; index < m_disabled_count; ++index) {
            if (std::strcmp(m_disabled[index], p_id) == 0) {
                (void)common::copy_string(m_disabled[index], DISABLED_ID_LEN, m_disabled[m_disabled_count - 1U]);
                --m_disabled_count;
                break;
            }
        }
    } else {
        if (m_disabled_count >= MAX_DISABLED) {
            return common::Status::ERR_FULL;
        }
        (void)common::copy_string(m_disabled[m_disabled_count++], DISABLED_ID_LEN, p_id);
        (void)close(p_id); // Đang mở thì đóng luôn.
    }
    ++m_registry_generation; // Launcher dựng lại lưới (ẩn / hiện icon).
    save_disabled();
    LOGI(TAG, "%s %s", is_enabled_now ? "enable" : "disable", p_id);
    return common::Status::OK;
}

void AppManager::load_disabled()
{
    char list[MAX_DISABLED * DISABLED_ID_LEN] = {};
    m_disabled_count = 0U;
    if (!common::is_ok(m_context.services.load_setting(storage_key::APPS_DISABLED, list, sizeof(list)))) {
        return;
    }
    char* p_item = list;
    while ((p_item != nullptr) && (*p_item != '\0') && (m_disabled_count < MAX_DISABLED)) {
        char* const p_comma = std::strchr(p_item, ',');
        if (p_comma != nullptr) {
            *p_comma = '\0';
        }
        if (*p_item != '\0') {
            (void)common::copy_string(m_disabled[m_disabled_count++], DISABLED_ID_LEN, p_item);
        }
        p_item = (p_comma != nullptr) ? (p_comma + 1) : nullptr;
    }
}

void AppManager::save_disabled()
{
    char list[MAX_DISABLED * DISABLED_ID_LEN] = {};
    size_t used = 0U;
    for (size_t index = 0U; index < m_disabled_count; ++index) {
        used += common::copy_string(&list[used], sizeof(list) - used, (index > 0U) ? "," : "");
        used += common::copy_string(&list[used], sizeof(list) - used, m_disabled[index]);
    }
    (void)m_context.services.save_setting(storage_key::APPS_DISABLED, list);
}

void AppManager::back()
{
    if (m_stack_depth <= 1U) {
        return; // Đang ở home.
    }
    if (m_stack[m_stack_depth - 1U].p_app->on_back()) {
        return;
    }
    pop_top();
    resume_top();
}

void AppManager::home()
{
    if (m_stack_depth <= 1U) {
        return;
    }
    while (m_stack_depth > 1U) {
        pop_top();
    }
    resume_top();
}

App* AppManager::find(const char* p_id) const
{
    if (p_id == nullptr) {
        return nullptr;
    }
    for (size_t index = 0U; index < m_app_count; ++index) {
        if (std::strcmp(m_p_apps[index]->info().p_id, p_id) == 0) {
            return m_p_apps[index];
        }
    }
    return nullptr;
}

common::Status AppManager::push(App& app)
{
    if (m_stack_depth >= config::MAX_APP_STACK) {
        LOGW(TAG, "open %s: back stack full (%u)", app.info().p_id, static_cast<unsigned>(config::MAX_APP_STACK));
        return common::Status::ERR_FULL;
    }
    lv_obj_t* const p_root = lv_obj_create(m_p_content);
    if (p_root == nullptr) {
        LOGE(TAG, "open %s: out of UI memory", app.info().p_id);
        return common::Status::ERR_NO_MEM;
    }
    lv_obj_remove_style_all(p_root);
    lv_obj_set_size(p_root, LV_PCT(100), LV_PCT(100));

    if (m_stack_depth > 0U) {
        StackEntry& top = m_stack[m_stack_depth - 1U];
        top.p_app->on_pause();
        lv_obj_set_hidden(top.p_root, true);
    }

    m_stack[m_stack_depth] = StackEntry{&app, p_root};
    m_stack_depth++;

    const uint32_t start_us = osal::time_us();
    TRACE_SCOPE("app", "open");
    app.on_create(p_root);
    app.on_resume();
    notify_foreground();
    EVENT_I("app", "open %s (%u ms, depth %u)", app.info().p_id,
         static_cast<unsigned>((osal::time_us() - start_us) / US_PER_MS), static_cast<unsigned>(m_stack_depth));
    return common::Status::OK;
}

void AppManager::pop_top()
{
    m_stack_depth--;
    StackEntry& top = m_stack[m_stack_depth];
    EVENT_I("app", "close %s", top.p_app->info().p_id);
    top.p_app->on_pause();
    top.p_app->on_destroy();
    // Xóa bất đồng bộ: an toàn khi back() được gọi từ chính event callback của app.
    lv_obj_delete_async(top.p_root);
    top = StackEntry{nullptr, nullptr};
}

void AppManager::remove_at(size_t index)
{
    if (index + 1U == m_stack_depth) {
        pop_top();
        return;
    }
    // App giữa stack đang ở trạng thái paused: chỉ cần destroy rồi dồn stack.
    StackEntry& entry = m_stack[index];
    entry.p_app->on_destroy();
    lv_obj_delete_async(entry.p_root);
    for (size_t slot = index; (slot + 1U) < m_stack_depth; ++slot) {
        m_stack[slot] = m_stack[slot + 1U];
    }
    m_stack_depth--;
    m_stack[m_stack_depth] = StackEntry{nullptr, nullptr};
}

void AppManager::resume_top()
{
    StackEntry& top = m_stack[m_stack_depth - 1U];
    lv_obj_set_hidden(top.p_root, false);
    top.p_app->on_resume();
    notify_foreground();
}

void AppManager::notify_foreground()
{
    App* const p_app = foreground();
    if ((m_p_listener != nullptr) && (p_app != nullptr)) {
        m_p_listener->on_foreground_changed(*p_app);
    }
}

} // namespace fw
