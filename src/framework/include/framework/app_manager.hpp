/**
 * @file    app_manager.hpp
 * @brief   Registry + back-stack ứng dụng (điều hướng kiểu điện thoại).
 *
 * @note    Mọi API chỉ được gọi từ ngữ cảnh UI task (callback LVGL) sau khi framework đã start,
 *          trừ register_app()/set_home() được gọi trong pha khởi tạo.
 */
#ifndef FRAMEWORK_APP_MANAGER_HPP
#define FRAMEWORK_APP_MANAGER_HPP

#include <cstddef>

#include "common/non_copyable.hpp"
#include "common/status.hpp"
#include "framework/app.hpp"
#include "framework/config.hpp"
#include "lvgl.h"

namespace fw {

/** @brief Nhận thông báo khi app foreground thay đổi (Shell dùng để cập nhật tiêu đề). */
class IAppListener {
public:
    virtual ~IAppListener() = default;
    virtual void on_foreground_changed(const App& app) = 0;
};

/** @brief Quản lý vòng đời & điều hướng app. */
class AppManager : private common::NonCopyable {
public:
    explicit AppManager(Services& services);

    /** @brief Đăng ký app (pha khởi tạo). @return ERR_FULL nếu vượt config::MAX_APPS. */
    common::Status register_app(App& app);

    /** @brief Chọn app home (đáy back-stack, không bao giờ bị đóng). */
    common::Status set_home(const char* p_id);

    /**
     * @brief Mở app. Nếu app đã nằm trong back-stack thì quay về app đó.
     * @return ERR_PARAM (không tồn tại), ERR_STATE (chưa start), ERR_FULL (stack đầy).
     */
    common::Status launch(const char* p_id);

    /** @brief Hành vi nút Back: hỏi app foreground, nếu không xử lý thì đóng app. */
    void back();

    /** @brief Hành vi nút Home: đóng mọi app phía trên home. */
    void home();

    /** @return Số app đã đăng ký. */
    size_t count() const { return m_app_count; }

    /** @return App thứ `index`, NULL nếu ngoài dải. */
    App* at(size_t index) const { return (index < m_app_count) ? m_p_apps[index] : nullptr; }

    /** @return App đang ở foreground, NULL nếu chưa start. */
    App* foreground() const { return (m_stack_depth > 0U) ? m_stack[m_stack_depth - 1U].p_app : nullptr; }

    /**
     * @brief [Nội bộ framework] Gắn vùng nội dung và mở app home.
     * @param[in] p_content  Container cha cho các app.
     * @param[in] p_listener Listener foreground (có thể NULL).
     */
    common::Status start(lv_obj_t* p_content, IAppListener* p_listener);

private:
    struct StackEntry {
        App* p_app;
        lv_obj_t* p_root;
    };

    App* find(const char* p_id) const;
    common::Status push(App& app);
    void pop_top();
    void resume_top();
    void notify_foreground();

    Context m_context;
    App* m_p_apps[config::MAX_APPS];
    size_t m_app_count;
    App* m_p_home;
    StackEntry m_stack[config::MAX_APP_STACK];
    size_t m_stack_depth;
    lv_obj_t* m_p_content;
    IAppListener* m_p_listener;
};

} // namespace fw

#endif /* FRAMEWORK_APP_MANAGER_HPP */
