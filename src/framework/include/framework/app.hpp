/**
 * @file    app.hpp
 * @brief   Base class ứng dụng + vòng đời (mô hình Activity của Android / View của S60).
 *
 * Vòng đời:  on_create -> on_resume <-> on_pause -> on_destroy
 *  - on_create : dựng UI vào `p_root` (container do framework sở hữu).
 *  - on_resume : app lên foreground (bật timer, refresh dữ liệu).
 *  - on_pause  : app bị che bởi app khác hoặc sắp đóng (dừng timer).
 *  - on_destroy: giải phóng tài nguyên KHÔNG thuộc cây `p_root` (lv_timer...).
 *                Framework tự xóa `p_root` và toàn bộ con của nó sau đó.
 */
#ifndef FRAMEWORK_APP_HPP
#define FRAMEWORK_APP_HPP

#include <cstdint>

#include "lvgl.h"

namespace fw {

class AppManager;
class Services;

/** @brief Ngữ cảnh framework cấp cho app (thay cho truy cập toàn cục). */
struct Context {
    AppManager& apps;   /**< Điều hướng: launch/back/home, liệt kê app. */
    Services& services; /**< Dịch vụ hệ thống: độ sáng, thông tin thiết bị... */
};

/** @brief Metadata tĩnh của app. */
struct AppInfo {
    const char* p_id;      /**< Định danh duy nhất, VD "settings". */
    const char* p_name;    /**< Tên hiển thị. */
    const char* p_icon;    /**< Ký hiệu LV_SYMBOL_*. */
    uint32_t color_hex;    /**< Màu icon trên launcher (0xRRGGBB). */
    bool show_in_launcher; /**< false cho app hệ thống (VD: chính launcher). */
};

/** @brief Lớp cơ sở mọi ứng dụng. App được cấp phát tĩnh, không new/delete. */
class App {
public:
    explicit App(const AppInfo& info) : m_info(info), m_p_context(nullptr) {}
    virtual ~App() = default;

    App(const App&) = delete;
    App& operator=(const App&) = delete;

    /** @return Metadata của app. */
    const AppInfo& info() const { return m_info; }

    /** @brief Dựng UI. @param[in] p_root Container toàn vùng nội dung. */
    virtual void on_create(lv_obj_t* p_root) = 0;
    virtual void on_resume() {}
    virtual void on_pause() {}
    virtual void on_destroy() {}

    /**
     * @brief Nút Back được nhấn khi app ở foreground.
     * @return true nếu app tự xử lý (VD: đóng dialog), false để framework đóng app.
     */
    virtual bool on_back() { return false; }

protected:
    /** @return Ngữ cảnh framework. Hợp lệ sau khi app được đăng ký. */
    Context& context() const { return *m_p_context; }

private:
    friend class AppManager;

    const AppInfo m_info;
    Context* m_p_context;
};

} // namespace fw

#endif /* FRAMEWORK_APP_HPP */
