/**
 * @file    script_app.cpp
 * @brief   Hiện thực slot app script.
 */
#include "script_app.hpp"

namespace runtime {

namespace {
constexpr fw::AppInfo EMPTY_INFO{"", "", "", 0U, false, true};
}

ScriptApp::ScriptApp() : fw::App(EMPTY_INFO), m_manifest{}, m_p_host(nullptr) {}

void ScriptApp::bind(const Manifest& manifest, ScriptHost& host)
{
    m_manifest = manifest;
    m_p_host = &host;
    // Chuỗi trỏ vào m_manifest -> sống cùng slot.
    set_info(fw::AppInfo{m_manifest.id, m_manifest.name, icon_symbol(m_manifest.icon), m_manifest.color_hex, true,
                         true /* exclusive: chỉ một máy ảo */});
}

void ScriptApp::on_create(lv_obj_t* p_root)
{
    if (m_p_host != nullptr) {
        (void)m_p_host->start(m_manifest, p_root, context().apps, context().services);
    }
}

void ScriptApp::on_resume()
{
    if (m_p_host != nullptr) {
        m_p_host->resume();
    }
}

void ScriptApp::on_pause()
{
    if (m_p_host != nullptr) {
        m_p_host->pause();
    }
}

void ScriptApp::on_destroy()
{
    if (m_p_host != nullptr) {
        m_p_host->stop();
    }
}

bool ScriptApp::on_back()
{
    return (m_p_host != nullptr) && m_p_host->back();
}

} // namespace runtime
