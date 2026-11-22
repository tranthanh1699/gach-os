/**
 * @file    esp32_clock.cpp
 * @brief   Hiện thực đồng hồ SNTP trên ESP-IDF.
 */
#include "esp32_mcal/esp32_clock.hpp"

#include <atomic>
#include <sys/time.h>

#include "esp_netif_sntp.h"

namespace esp32_mcal {

namespace {

// Callback SNTP chạy trong task lwIP -> cờ atomic.
std::atomic<bool> s_is_synced{false};

void on_time_synced(struct timeval* p_tv)
{
    (void)p_tv;
    s_is_synced.store(true);
}

} // namespace

common::Status Esp32Clock::start_sync(const char* p_server)
{
    if (p_server == nullptr) {
        return common::Status::ERR_PARAM;
    }
    if (m_is_started) {
        return common::Status::OK; // SNTP tự đồng bộ lại định kỳ; không cần khởi động lần hai.
    }
    esp_sntp_config_t cfg{};
    cfg.smooth_sync = false;
    cfg.server_from_dhcp = false;
    cfg.wait_for_sync = false;
    cfg.start = true;
    cfg.sync_cb = &on_time_synced;
    cfg.renew_servers_after_new_IP = false;
    cfg.ip_event_to_renew = IP_EVENT_STA_GOT_IP;
    cfg.index_of_first_server = 0U;
    cfg.num_of_servers = 1U;
    cfg.servers[0] = p_server;

    if (esp_netif_sntp_init(&cfg) != ESP_OK) {
        return common::Status::ERR_IO;
    }
    m_is_started = true;
    return common::Status::OK;
}

bool Esp32Clock::is_synced() const
{
    return s_is_synced.load();
}

bool Esp32Clock::utc_seconds(int64_t& seconds) const
{
    if (!is_synced()) {
        return false;
    }
    struct timeval now {};
    (void)gettimeofday(&now, nullptr);
    seconds = static_cast<int64_t>(now.tv_sec);
    return true;
}

} // namespace esp32_mcal
