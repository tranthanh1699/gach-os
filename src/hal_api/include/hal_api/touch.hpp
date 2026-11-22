/**
 * @file    touch.hpp
 * @brief   HAL contract: cảm ứng điện trở/điện dung một điểm.
 */
#ifndef HAL_API_TOUCH_HPP
#define HAL_API_TOUCH_HPP

#include <cstdint>

#include "common/status.hpp"

namespace hal {

/** @brief Một mẫu cảm ứng, tọa độ đã quy về không gian pixel của IDisplay. */
struct TouchPoint {
    uint16_t x;
    uint16_t y;
    bool pressed;
};

/** @brief Bộ điều khiển cảm ứng (XPT2046, GT911, FT6x36...). */
class ITouch {
public:
    virtual ~ITouch() = default;

    /** @brief Khởi tạo controller. */
    virtual common::Status init() = 0;

    /**
     * @brief Đọc trạng thái cảm ứng hiện tại (đã hiệu chuẩn, đã lọc nhiễu).
     * @param[out] point Kết quả; `pressed == false` khi không chạm.
     */
    virtual common::Status read(TouchPoint& point) = 0;
};

} // namespace hal

#endif /* HAL_API_TOUCH_HPP */
