/**
 * @file    display.hpp
 * @brief   HAL contract: màn hình đồ họa (độc lập controller ILI9341/ST7789/RGB...).
 */
#ifndef HAL_API_DISPLAY_HPP
#define HAL_API_DISPLAY_HPP

#include <cstdint>

#include "common/status.hpp"

namespace hal {

/** @brief Vùng chữ nhật, tọa độ bao gồm cả 2 đầu (inclusive). */
struct Area {
    int32_t x1;
    int32_t y1;
    int32_t x2;
    int32_t y2;
};

/** @brief Định dạng pixel mà panel mong đợi trên đường truyền. */
enum class PixelFormat : uint8_t {
    RGB565,        /**< RGB565 little-endian (thứ tự bộ nhớ CPU). */
    RGB565_SWAPPED /**< RGB565 big-endian (đa số panel SPI: ILI9341, ST7789). */
};

/** @brief Màn hình đồ họa. Tầng framework chỉ giao tiếp qua interface này. */
class IDisplay {
public:
    virtual ~IDisplay() = default;

    /** @brief Khởi tạo panel (reset, init sequence, bật hiển thị). */
    virtual common::Status init() = 0;

    /** @return Chiều rộng logic (pixel) sau khi áp hướng xoay của board. */
    virtual uint16_t width() const = 0;

    /** @return Chiều cao logic (pixel) sau khi áp hướng xoay của board. */
    virtual uint16_t height() const = 0;

    /** @return Định dạng pixel mà draw_bitmap() yêu cầu. */
    virtual PixelFormat pixel_format() const = 0;

    /**
     * @brief Đẩy một vùng pixel lên panel (blocking tới khi truyền xong).
     *
     * @param[in] area     Vùng đích (inclusive), nằm trong [0,width) x [0,height).
     * @param[in] p_pixels Dữ liệu pixel theo pixel_format(), số phần tử = diện tích vùng.
     */
    virtual common::Status draw_bitmap(const Area& area, const uint16_t* p_pixels) = 0;
};

} // namespace hal

#endif /* HAL_API_DISPLAY_HPP */
