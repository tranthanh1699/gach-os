/**
 * @file    time_service.hpp
 * @brief   Dịch vụ thời gian: giờ địa phương từ đồng hồ đã đồng bộ (NTP Google) + múi giờ.
 */
#ifndef FRAMEWORK_TIME_SERVICE_HPP
#define FRAMEWORK_TIME_SERVICE_HPP

#include <cstdint>

#include "common/non_copyable.hpp"
#include "common/status.hpp"

namespace hal {
struct Devices;
}

namespace fw {

/** @brief Ngày giờ dân sự (lịch Gregory). */
struct DateTime {
    uint16_t year;
    uint8_t month;   /**< 1..12 */
    uint8_t day;     /**< 1..31 */
    uint8_t hour;    /**< 0..23 */
    uint8_t minute;  /**< 0..59 */
    uint8_t second;  /**< 0..59 */
    uint8_t weekday; /**< 0 = Chủ nhật ... 6 = Thứ bảy */
};

/**
 * @brief Đổi số giây Unix epoch sang ngày giờ (thuần tính toán, không phụ thuộc libc/múi giờ).
 * @param[in]  epoch_seconds Giây kể từ 1970-01-01 00:00:00.
 * @param[out] out           Kết quả.
 */
void epoch_to_datetime(int64_t epoch_seconds, DateTime& out);

/** @brief Dịch vụ thời gian. */
class TimeService : private common::NonCopyable {
public:
    TimeService();

    /** @return true khi đồng hồ đã được đồng bộ qua mạng. */
    bool is_synced() const;

    /**
     * @brief Giờ địa phương hiện tại.
     * @return false nếu chưa đồng bộ.
     */
    bool now(DateTime& out) const;

    /** @return Độ lệch múi giờ (phút so với UTC). */
    int32_t utc_offset_minutes() const { return m_utc_offset_min; }

    /** @brief Đặt và lưu múi giờ. @param[in] minutes -720..+840. */
    common::Status set_utc_offset_minutes(int32_t minutes);

    /** @brief [Nội bộ framework] Gắn thiết bị, nạp múi giờ đã lưu. */
    void bind(const hal::Devices& devices);

    /** @brief [Nội bộ framework] Mạng vừa sẵn sàng -> yêu cầu đồng bộ NTP. */
    void on_network_up();

private:
    const hal::Devices* m_p_devices;
    int32_t m_utc_offset_min;
};

} // namespace fw

#endif /* FRAMEWORK_TIME_SERVICE_HPP */
