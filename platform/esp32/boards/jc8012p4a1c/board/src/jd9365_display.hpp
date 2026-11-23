/**
 * @file    jd9365_display.hpp
 * @brief   hal::IDisplay cho panel MIPI-DSI JD9365 (video mode/DPI) + xoay phần cứng bằng PPA.
 *
 * Panel DPI quét liên tục từ framebuffer trong PSRAM. draw_bitmap() dùng PPA SRM xoay vùng
 * LVGL vừa render rồi ghi thẳng vào framebuffer (không cần buffer trung gian, CPU không chạm pixel).
 */
#ifndef BOARD_JD9365_DISPLAY_HPP
#define BOARD_JD9365_DISPLAY_HPP

#include <cstddef>
#include <cstdint>

#include "common/non_copyable.hpp"
#include "driver/gpio.h"
#include "driver/ppa.h"
#include "esp_lcd_mipi_dsi.h"
#include "esp_lcd_types.h"
#include "esp_ldo_regulator.h"
#include "hal_api/display.hpp"
#include "rotation.hpp"

namespace board {

/** @brief Timing video DPI (porch/pulse tính theo pixel clock với trục ngang, theo dòng với trục dọc). */
struct DpiTiming {
    uint32_t pixel_clock_mhz;
    uint16_t hsync_pulse_width;
    uint16_t hsync_back_porch;
    uint16_t hsync_front_porch;
    uint16_t vsync_pulse_width;
    uint16_t vsync_back_porch;
    uint16_t vsync_front_porch;
};

/** @brief Cấu hình panel JD9365 trên bus MIPI-DSI. */
struct Jd9365Config {
    uint16_t panel_width;         /**< Độ phân giải gốc của panel (hướng dọc). */
    uint16_t panel_height;
    Rotation rotation;            /**< Xoay từ logic sang vật lý (ngược chiều kim đồng hồ). */
    bool scan_mirror_x;           /**< Lật quét panel bằng phần cứng (MADCTL GS), áp lúc init. */
    bool scan_mirror_y;           /**< Lật quét panel bằng phần cứng (MADCTL SS), áp lúc init. */
    gpio_num_t reset_pin;
    uint8_t dsi_lane_count;
    uint32_t dsi_lane_bitrate_mbps;
    DpiTiming timing;
    int phy_ldo_channel;          /**< LDO on-chip cấp VDD_MIPI_DPHY. */
    int phy_ldo_voltage_mv;
};

/** @brief Màn hình JD9365 800x1280, xuất ra OS theo hướng đã xoay. */
class Jd9365Display final : public hal::IDisplay, private common::NonCopyable {
public:
    explicit Jd9365Display(const Jd9365Config& cfg);

    common::Status init() override;
    uint16_t width() const override;
    uint16_t height() const override;
    hal::PixelFormat pixel_format() const override { return hal::PixelFormat::RGB565; }
    common::Status draw_bitmap(const hal::Area& area, const uint16_t* p_pixels) override;
    bool supports_async_flush() const override { return true; }
    common::Status draw_bitmap_async(const hal::Area& area, const uint16_t* p_pixels, hal::FlushDoneCallback on_done,
                                     void* p_ctx) override;

private:
    common::Status init_panel();
    common::Status init_rotator();
    common::Status rotate(const hal::Area& area, const uint16_t* p_pixels, bool is_async);
    static bool on_ppa_done(ppa_client_handle_t p_client, ppa_event_data_t* p_event, void* p_user_data);

    const Jd9365Config m_cfg;
    esp_ldo_channel_handle_t m_p_phy_ldo;
    esp_lcd_dsi_bus_handle_t m_p_dsi_bus;
    esp_lcd_panel_io_handle_t m_p_io;
    esp_lcd_panel_handle_t m_p_panel;
    ppa_client_handle_t m_p_ppa;
    void* m_p_frame_buffer;
    size_t m_frame_buffer_bytes;
    hal::FlushDoneCallback volatile m_on_done; /**< Lần đẩy bất đồng bộ đang chạy (đọc trong ISR). */
    void* volatile m_p_done_ctx;
};

} // namespace board

#endif /* BOARD_JD9365_DISPLAY_HPP */
