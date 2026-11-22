/**
 * @file    clock.hpp
 * @brief   HAL contract: đồng hồ thời gian thực, đồng bộ qua mạng (SNTP).
 */
#ifndef HAL_API_CLOCK_HPP
#define HAL_API_CLOCK_HPP

#include <cstdint>

#include "common/status.hpp"

namespace hal {

/** @brief Đồng hồ hệ thống (UTC). */
class IClock {
public:
    virtual ~IClock() = default;

    /**
     * @brief Bắt đầu đồng bộ thời gian qua mạng (không chặn, gọi lại nhiều lần an toàn).
     * @param[in] p_server Hostname NTP. Con trỏ phải sống suốt chương trình (chuỗi literal).
     */
    virtual common::Status start_sync(const char* p_server) = 0;

    /** @return true khi đã đồng bộ thành công ít nhất một lần. */
    virtual bool is_synced() const = 0;

    /**
     * @brief Thời gian hiện tại.
     * @param[out] seconds Số giây UTC kể từ 1970-01-01 (Unix epoch).
     * @return false nếu đồng hồ chưa hợp lệ (chưa đồng bộ).
     */
    virtual bool utc_seconds(int64_t& seconds) const = 0;
};

} // namespace hal

#endif /* HAL_API_CLOCK_HPP */
