/**
 * @file    script_app.hpp
 * @brief   [Private] fw::App đại diện cho một gói đã cài – chuyển vòng đời sang ScriptHost.
 */
#ifndef RUNTIME_SCRIPT_APP_HPP
#define RUNTIME_SCRIPT_APP_HPP

#include "framework/app.hpp"
#include "manifest.hpp"
#include "script_host.hpp"

namespace runtime {

/** @brief Slot app script (cấp phát tĩnh, tái sử dụng khi gỡ/cài lại). */
class ScriptApp final : public fw::App {
public:
    ScriptApp();

    /** @brief Gắn gói vào slot (gọi TRƯỚC khi đăng ký với AppManager). */
    void bind(const Manifest& manifest, ScriptHost& host);
    void unbind() { m_p_host = nullptr; }
    bool is_bound() const { return m_p_host != nullptr; }
    const Manifest& manifest() const { return m_manifest; }

    void on_create(lv_obj_t* p_root) override;
    void on_resume() override;
    void on_pause() override;
    void on_destroy() override;
    bool on_back() override;

private:
    Manifest m_manifest;
    ScriptHost* m_p_host;
};

} // namespace runtime

#endif /* RUNTIME_SCRIPT_APP_HPP */
