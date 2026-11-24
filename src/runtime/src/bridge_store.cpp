/**
 * @file    bridge_store.cpp
 * @brief   Lệnh `store` của debug bridge: địa chỉ kho, tải danh mục, liệt kê, cài app từ kho trực tuyến.
 *
 * API kho chạy trên task UI: mọi bước gọi qua bridge_run_on_ui(); bridge chờ việc tải bằng cách hỏi store_poll()
 * định kỳ (tải chạy trên task HTTP của platform, không chặn UI).
 */
#include <cstdio>
#include <cstring>

#include "bridge_sys.hpp"
#include "osal/osal.hpp"
#include "runtime/runtime.hpp"

namespace runtime {

namespace {

constexpr size_t LINE_LEN = 160U;
constexpr uint32_t POLL_MS = 250U;
constexpr uint32_t PROGRESS_EVERY_MS = 2000U;
constexpr uint32_t TIMEOUT_MS = 180000U; /**< Gói lớn qua Wi-Fi chậm: chờ tối đa 3 phút. */
constexpr size_t KIB = 1024U;

struct Job {
    const char* p_arg;
    size_t index;
    common::Status status;
    StoreState state;
};

Job s_job;

void ui_refresh(void* p_ctx) { static_cast<Job*>(p_ctx)->status = store_refresh(); }
void ui_install(void* p_ctx)
{
    auto* const p_job = static_cast<Job*>(p_ctx);
    p_job->status = store_install(p_job->index);
}
void ui_poll(void* p_ctx) { static_cast<Job*>(p_ctx)->state = store_poll(); }
void ui_set_url(void* p_ctx)
{
    auto* const p_job = static_cast<Job*>(p_ctx);
    p_job->status = store_set_url(p_job->p_arg) ? common::Status::OK : common::Status::ERR_PARAM;
}

/** @brief Chờ việc của kho xong (DONE / FAILED), in tiến độ tải. */
bool wait_done(uint8_t id)
{
    const uint32_t start = osal::tick_ms();
    uint32_t last_progress = start;
    do {
        osal::delay_ms(POLL_MS);
        if (!bridge_run_on_ui(&ui_poll, &s_job)) {
            bridge_resp(id, false, "UI not responding");
            return false;
        }
        if ((s_job.state == StoreState::DOWNLOADING) && ((osal::tick_ms() - last_progress) >= PROGRESS_EVERY_MS)) {
            last_progress = osal::tick_ms();
            size_t received = 0U;
            size_t total = 0U;
            store_progress(received, total);
            char text[LINE_LEN];
            (void)std::snprintf(text, sizeof(text), "  %u / %u KB", static_cast<unsigned>(received / KIB),
                                static_cast<unsigned>(total / KIB));
            bridge_text(id, text);
        }
    } while (((s_job.state == StoreState::LOADING_INDEX) || (s_job.state == StoreState::DOWNLOADING)) &&
             ((osal::tick_ms() - start) < TIMEOUT_MS));
    const bool ok = (s_job.state == StoreState::DONE);
    bridge_resp(id, ok, ok ? store_message() : ((store_message()[0] != '\0') ? store_message() : "timeout"));
    return ok;
}

void cmd_list(uint8_t id)
{
    char text[LINE_LEN];
    (void)std::snprintf(text, sizeof(text), "%-26s %-18s %-9s %-4s %8s  %s", "ID", "NAME", "VERSION", "SDK", "SIZE",
                        "STATE");
    bridge_text(id, text);
    for (size_t index = 0U; index < store_count(); ++index) {
        const StoreApp& app = store_app(index);
        PackageDetails installed{};
        const bool has = package_details(app.id, installed);
        const char* const p_state = !has ? "-" : ((std::strcmp(installed.p_version, app.version) == 0) ? "installed"
                                                                                                        : "update");
        (void)std::snprintf(text, sizeof(text), "%-26s %-18s %-9s v%-3u %6u KB  %s", app.id, app.name, app.version,
                            static_cast<unsigned>(app.sdk), static_cast<unsigned>((app.size + KIB - 1U) / KIB),
                            p_state);
        bridge_text(id, text);
    }
    (void)std::snprintf(text, sizeof(text), "%u app(s) from %s", static_cast<unsigned>(store_count()), store_url());
    bridge_resp(id, true, text);
}

bool refresh(uint8_t id, bool send_resp)
{
    if (!bridge_run_on_ui(&ui_refresh, &s_job) || !common::is_ok(s_job.status)) {
        bridge_resp(id, false, (store_message()[0] != '\0') ? store_message() : "store busy");
        return false;
    }
    if (!send_resp) { // Chờ im lặng rồi để lệnh gọi trả kết quả.
        do {
            osal::delay_ms(POLL_MS);
            (void)bridge_run_on_ui(&ui_poll, &s_job);
        } while (s_job.state == StoreState::LOADING_INDEX);
        if (s_job.state != StoreState::DONE) {
            bridge_resp(id, false, store_message());
            return false;
        }
        return true;
    }
    return wait_done(id);
}

void cmd_install(uint8_t id, const char* p_app)
{
    if (p_app == nullptr) {
        bridge_resp(id, false, "usage: store install <id>");
        return;
    }
    if ((store_count() == 0U) && !refresh(id, false)) {
        return;
    }
    for (size_t index = 0U; index < store_count(); ++index) {
        if (std::strcmp(store_app(index).id, p_app) == 0) {
            s_job.index = index;
            if (!bridge_run_on_ui(&ui_install, &s_job) || !common::is_ok(s_job.status)) {
                bridge_resp(id, false, (store_message()[0] != '\0') ? store_message() : "store busy");
                return;
            }
            (void)wait_done(id);
            return;
        }
    }
    bridge_resp(id, false, "no such app in the store (store list)");
}

} // namespace

bool bridge_store_command(uint8_t id, size_t argc, char* const p_args[])
{
    if ((argc == 0U) || (std::strcmp(p_args[0], "store") != 0)) {
        return false;
    }
    const char* const p_sub = (argc > 1U) ? p_args[1] : "list";
    const char* const p_a1 = (argc > 2U) ? p_args[2] : nullptr;
    if (std::strcmp(p_sub, "url") == 0) {
        if (p_a1 != nullptr) {
            s_job.p_arg = (std::strcmp(p_a1, "default") == 0) ? "" : p_a1;
            if (!bridge_run_on_ui(&ui_set_url, &s_job) || !common::is_ok(s_job.status)) {
                bridge_resp(id, false, "url must start with http:// or https://");
                return true;
            }
        }
        bridge_resp(id, true, store_url());
    } else if (std::strcmp(p_sub, "refresh") == 0) {
        (void)refresh(id, true);
    } else if (std::strcmp(p_sub, "list") == 0) {
        if ((store_count() > 0U) || refresh(id, false)) {
            cmd_list(id);
        }
    } else if (std::strcmp(p_sub, "install") == 0) {
        cmd_install(id, p_a1);
    } else {
        bridge_resp(id, false, "usage: store url [url|default] | refresh | list | install <id>");
    }
    return true;
}

} // namespace runtime
