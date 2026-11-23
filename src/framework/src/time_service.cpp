/**
 * @file    time_service.cpp
 * @brief   Hiện thực dịch vụ thời gian.
 */
#include "framework/time_service.hpp"

#include "common/log.hpp"
#include "framework/config.hpp"
#include "hal_api/board.hpp"
#include "storage_keys.hpp"

namespace fw {

namespace {

constexpr int64_t SECONDS_PER_DAY = 86400;
constexpr int64_t SECONDS_PER_HOUR = 3600;
constexpr int64_t SECONDS_PER_MINUTE = 60;
constexpr int32_t UTC_OFFSET_MIN_LIMIT = -12 * 60;
constexpr int32_t UTC_OFFSET_MAX_LIMIT = 14 * 60;
constexpr int64_t EPOCH_WEEKDAY = 4; // 1970-01-01 là thứ Năm.
constexpr int64_t DAYS_PER_WEEK = 7;

// Thuật toán "civil_from_days" của Howard Hinnant – đúng cho mọi ngày lịch Gregory.
constexpr int64_t DAYS_FROM_0000_TO_1970 = 719468;
constexpr int64_t DAYS_PER_ERA = 146097; // 400 năm.

int64_t floor_div(int64_t value, int64_t divisor)
{
    const int64_t quotient = value / divisor;
    return ((value % divisor) < 0) ? (quotient - 1) : quotient;
}

} // namespace

void epoch_to_datetime(int64_t epoch_seconds, DateTime& out)
{
    const int64_t days = floor_div(epoch_seconds, SECONDS_PER_DAY);
    const int64_t secs_of_day = epoch_seconds - (days * SECONDS_PER_DAY);

    const int64_t shifted = days + DAYS_FROM_0000_TO_1970;
    const int64_t era = floor_div(shifted, DAYS_PER_ERA);
    const int64_t day_of_era = shifted - (era * DAYS_PER_ERA);                                       // [0, 146096]
    const int64_t year_of_era = (day_of_era - day_of_era / 1460 + day_of_era / 36524 - day_of_era / 146096) / 365;
    const int64_t day_of_year = day_of_era - (365 * year_of_era + year_of_era / 4 - year_of_era / 100); // [0, 365]
    const int64_t month_index = (5 * day_of_year + 2) / 153;                                          // 0 = tháng 3
    const int64_t day = day_of_year - (153 * month_index + 2) / 5 + 1;
    const int64_t month = (month_index < 10) ? (month_index + 3) : (month_index - 9);
    const int64_t year = year_of_era + era * 400 + ((month <= 2) ? 1 : 0);

    out.year = static_cast<uint16_t>(year);
    out.month = static_cast<uint8_t>(month);
    out.day = static_cast<uint8_t>(day);
    out.hour = static_cast<uint8_t>(secs_of_day / SECONDS_PER_HOUR);
    out.minute = static_cast<uint8_t>((secs_of_day % SECONDS_PER_HOUR) / SECONDS_PER_MINUTE);
    out.second = static_cast<uint8_t>(secs_of_day % SECONDS_PER_MINUTE);
    const int64_t weekday = (days + EPOCH_WEEKDAY) % DAYS_PER_WEEK;
    out.weekday = static_cast<uint8_t>((weekday < 0) ? (weekday + DAYS_PER_WEEK) : weekday);
}

TimeService::TimeService() : m_p_devices(nullptr), m_utc_offset_min(config::DEFAULT_UTC_OFFSET_MIN) {}

void TimeService::bind(const hal::Devices& devices)
{
    m_p_devices = &devices;
    int32_t stored = 0;
    if ((devices.p_storage != nullptr) &&
        common::is_ok(devices.p_storage->get_i32(storage_key::UTC_OFFSET_MIN, stored)) &&
        (stored >= UTC_OFFSET_MIN_LIMIT) && (stored <= UTC_OFFSET_MAX_LIMIT)) {
        m_utc_offset_min = stored;
    }
}

bool TimeService::is_synced() const
{
    return (m_p_devices != nullptr) && (m_p_devices->p_clock != nullptr) && m_p_devices->p_clock->is_synced();
}

bool TimeService::now(DateTime& out) const
{
    int64_t utc = 0;
    if (!is_synced() || !m_p_devices->p_clock->utc_seconds(utc)) {
        return false;
    }
    epoch_to_datetime(utc + static_cast<int64_t>(m_utc_offset_min) * SECONDS_PER_MINUTE, out);
    return true;
}

common::Status TimeService::set_utc_offset_minutes(int32_t minutes)
{
    if ((minutes < UTC_OFFSET_MIN_LIMIT) || (minutes > UTC_OFFSET_MAX_LIMIT)) {
        return common::Status::ERR_PARAM;
    }
    m_utc_offset_min = minutes;
    if ((m_p_devices != nullptr) && (m_p_devices->p_storage != nullptr)) {
        return m_p_devices->p_storage->set_i32(storage_key::UTC_OFFSET_MIN, minutes);
    }
    return common::Status::OK;
}

void TimeService::on_network_up()
{
    if ((m_p_devices != nullptr) && (m_p_devices->p_clock != nullptr)) {
        const common::Status status = m_p_devices->p_clock->start_sync(config::NTP_SERVER);
        if (common::is_ok(status)) {
            LOGI("time", "NTP sync started (%s)", config::NTP_SERVER);
        } else {
            LOGW("time", "NTP sync failed to start (status=%u)", static_cast<unsigned>(status));
        }
    }
}

} // namespace fw
