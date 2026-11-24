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

/**
 * @brief Báo một lần đẩy pixel bất đồng bộ đã xong (buffer nguồn dùng lại được).
 * @note Có thể được gọi từ ngữ cảnh ngắt: chỉ được làm việc ngắn, an toàn ISR.
 */
using FlushDoneCallback = void (*)(void* p_ctx);

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

    /** @return true nếu hỗ trợ draw_bitmap_async() (khi đó framework dùng double buffering). */
    virtual bool supports_async_flush() const { return false; }

    /**
     * @brief Bắt đầu đẩy một vùng pixel rồi trả về ngay; on_done(p_ctx) được gọi khi xong (có thể từ ISR).
     *
     * Framework vẽ dải kế tiếp vào buffer khác trong lúc phần cứng đẩy dải này. Tối đa một lần đẩy đang chạy:
     * framework luôn chờ on_done trước khi gọi lần kế tiếp. Lỗi khởi động -> trả lỗi, on_done không được gọi.
     */
    virtual common::Status draw_bitmap_async(const Area& area, const uint16_t* p_pixels, FlushDoneCallback on_done,
                                             void* p_ctx)
    {
        (void)area;
        (void)p_pixels;
        (void)on_done;
        (void)p_ctx;
        return common::Status::ERR_NOT_SUPPORTED;
    }

    /**
     * @brief Đọc lại nội dung đang hiển thị (chụp màn hình): một hàng logic, thu nhỏ theo scale (lấy mẫu điểm).
     * @param[in]  row   Hàng của ảnh thu nhỏ (0 .. height()/scale - 1). Hàng 0 bắt đầu một lần chụp.
     * @param[in]  scale Hệ số thu nhỏ (>= 1).
     * @param[out] p_out width()/scale pixel RGB565 (byte thấp trước).
     * @return ERR_NOT_SUPPORTED nếu màn hình không đọc lại được (VD panel SPI không có framebuffer).
     */
    virtual common::Status read_row_scaled(uint32_t row, uint32_t scale, uint16_t* p_out)
    {
        (void)row;
        (void)scale;
        (void)p_out;
        return common::Status::ERR_NOT_SUPPORTED;
    }
};

} // namespace hal

#endif /* HAL_API_DISPLAY_HPP */
