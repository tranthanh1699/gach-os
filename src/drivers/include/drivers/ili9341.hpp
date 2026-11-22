/**
 * @file    ili9341.hpp
 * @brief   Driver panel ILI9341 qua SPI 4-dây (portable – chỉ phụ thuộc MCAL contract).
 */
#ifndef DRIVERS_ILI9341_HPP
#define DRIVERS_ILI9341_HPP

#include <cstddef>
#include <cstdint>

#include "common/non_copyable.hpp"
#include "hal_api/display.hpp"
#include "mcal/gpio.hpp"
#include "mcal/spi.hpp"

namespace drivers {

/** @brief Cấu hình panel. Các cờ hướng phụ thuộc cách board gắn panel. */
struct Ili9341Config {
    uint16_t width;  /**< Chiều rộng logic sau xoay (VD: 320 khi landscape). */
    uint16_t height; /**< Chiều cao logic sau xoay (VD: 240 khi landscape). */
    bool swap_xy;    /**< MADCTL.MV – hoán đổi trục (landscape). */
    bool mirror_x;   /**< MADCTL.MX. */
    bool mirror_y;   /**< MADCTL.MY. */
    bool bgr;        /**< MADCTL.BGR – thứ tự màu panel. */
    bool invert;     /**< Bật INVON (một số module cần đảo màu). */
    bool vendor_init; /**< Gửi chuỗi power/gamma riêng ILI9341. false cho ST7789 hoặc khi chưa rõ chip. */
};

/**
 * @brief Panel SPI theo chuẩn MIPI DCS: ILI9341 và ST7789 (240x320), hiện thực hal::IDisplay.
 *
 * Hai controller dùng chung bộ lệnh DCS (SLPOUT/COLMOD/MADCTL/CASET/PASET/RAMWR); khác biệt duy nhất
 * là chuỗi power/gamma riêng của ILI9341 – bật qua Ili9341Config::vendor_init.
 */
class Ili9341 final : public hal::IDisplay, private common::NonCopyable {
public:
    /**
     * @param[in] spi   Thiết bị SPI của panel (CS do platform quản lý).
     * @param[in] dc    Chân Data/Command.
     * @param[in] p_rst Chân reset cứng, NULL nếu board nối RST vào EN.
     * @param[in] cfg   Cấu hình panel.
     */
    Ili9341(mcal::ISpiDevice& spi, mcal::IGpioOutput& dc, mcal::IGpioOutput* p_rst, const Ili9341Config& cfg);

    common::Status init() override;
    uint16_t width() const override { return m_cfg.width; }
    uint16_t height() const override { return m_cfg.height; }
    hal::PixelFormat pixel_format() const override { return hal::PixelFormat::RGB565_SWAPPED; }
    common::Status draw_bitmap(const hal::Area& area, const uint16_t* p_pixels) override;

private:
    common::Status send_command(uint8_t cmd, const uint8_t* p_data, size_t len);
    common::Status set_window(const hal::Area& area);
    uint8_t build_madctl() const;

    mcal::ISpiDevice& m_spi;
    mcal::IGpioOutput& m_dc;
    mcal::IGpioOutput* m_p_rst;
    Ili9341Config m_cfg;
    bool m_is_initialized;
};

} // namespace drivers

#endif /* DRIVERS_ILI9341_HPP */
