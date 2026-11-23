/**
 * @file    app_manager.cpp
 * @brief   Hiện thực registry + back-stack ứng dụng.
 */
#include "framework/app_manager.hpp"

#include <cstring>

namespace fw {

AppManager::AppManager(Services& services)
    : m_context{*this, services}, m_p_apps{}, m_app_count(0U), m_registry_generation(0U), m_p_home(nullptr),
      m_stack{}, m_stack_depth(0U),
      m_p_content(nullptr), m_p_listener(nullptr)
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
        return common::Status::ERR_PARAM;
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
        return common::Status::ERR_FULL;
    }
    lv_obj_t* const p_root = lv_obj_create(m_p_content);
    if (p_root == nullptr) {
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

    app.on_create(p_root);
    app.on_resume();
    notify_foreground();
    return common::Status::OK;
}

void AppManager::pop_top()
{
    m_stack_depth--;
    StackEntry& top = m_stack[m_stack_depth];
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
