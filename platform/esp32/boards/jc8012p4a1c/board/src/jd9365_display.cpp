/**
 * @file    jd9365_display.cpp
 * @brief   Khởi tạo JD9365 (LDO D-PHY -> DSI bus -> DBI cmd IO -> DPI panel) và xoay bằng PPA.
 *
 * Trình tự init port từ BSP của hãng: vendor/esp32_p4_function_ev_board (bsp_display_new_with_handles).
 */
#include "jd9365_display.hpp"

#include "esp_lcd_jd9365.h"
#include "jd9365_init_cmds.h"
#include "esp_lcd_panel_ops.h"
#include "esp_log.h"
#include "esp_private/esp_cache_private.h"
#include "esp_heap_caps.h"

namespace board {

namespace {

constexpr const char* TAG = "jd9365";
constexpr uint8_t DSI_BUS_ID = 0U;
constexpr uint8_t DBI_CMD_BITS = 8U;   /**< Lệnh DCS 8 bit (theo datasheet JD9365). */
constexpr uint8_t DBI_PARAM_BITS = 8U;
constexpr uint32_t RGB565_BITS = 16U;
constexpr size_t RGB565_BYTES = 2U;
constexpr uint32_t FRAME_BUFFER_COUNT = 1U;
constexpr float PPA_SCALE_NONE = 1.0F;

ppa_srm_rotation_angle_t to_ppa_angle(Rotation rot)
{
    switch (rot) {
    case Rotation::DEG_90:
        return PPA_SRM_ROTATION_ANGLE_90;
    case Rotation::DEG_180:
        return PPA_SRM_ROTATION_ANGLE_180;
    case Rotation::DEG_270:
        return PPA_SRM_ROTATION_ANGLE_270;
    case Rotation::DEG_0:
    default:
        return PPA_SRM_ROTATION_ANGLE_0;
    }
}

/** @brief Ghi log + quy lỗi ESP-IDF về common::Status. */
bool check(esp_err_t err, const char* p_what)
{
    if (err != ESP_OK) {
        ESP_LOGE(TAG, "%s failed: %s", p_what, esp_err_to_name(err));
        return false;
    }
    return true;
}

} // namespace

Jd9365Display::Jd9365Display(const Jd9365Config& cfg)
    : m_cfg(cfg), m_p_phy_ldo(nullptr), m_p_dsi_bus(nullptr), m_p_io(nullptr), m_p_panel(nullptr), m_p_ppa(nullptr),
      m_p_frame_buffer(nullptr), m_frame_buffer_bytes(0U), m_on_done(nullptr), m_p_done_ctx(nullptr)
{
}

uint16_t Jd9365Display::width() const
{
    return rotation::swaps_axes(m_cfg.rotation) ? m_cfg.panel_height : m_cfg.panel_width;
}

uint16_t Jd9365Display::height() const
{
    return rotation::swaps_axes(m_cfg.rotation) ? m_cfg.panel_width : m_cfg.panel_height;
}

common::Status Jd9365Display::init()
{
    if (m_p_panel != nullptr) {
        return common::Status::ERR_STATE;
    }
    const common::Status status = init_panel();
    if (!common::is_ok(status)) {
        return status;
    }
    return init_rotator();
}

common::Status Jd9365Display::init_panel()
{
    // D-PHY cần nguồn từ LDO on-chip trước khi tạo bus DSI.
    esp_ldo_channel_config_t ldo_cfg{};
    ldo_cfg.chan_id = m_cfg.phy_ldo_channel;
    ldo_cfg.voltage_mv = m_cfg.phy_ldo_voltage_mv;
    if (!check(esp_ldo_acquire_channel(&ldo_cfg, &m_p_phy_ldo), "D-PHY LDO")) {
        return common::Status::ERR_IO;
    }

    esp_lcd_dsi_bus_config_t bus_cfg{};
    bus_cfg.bus_id = DSI_BUS_ID;
    bus_cfg.num_data_lanes = m_cfg.dsi_lane_count;
    bus_cfg.phy_clk_src = MIPI_DSI_PHY_CLK_SRC_DEFAULT;
    bus_cfg.lane_bit_rate_mbps = m_cfg.dsi_lane_bitrate_mbps;
    if (!check(esp_lcd_new_dsi_bus(&bus_cfg, &m_p_dsi_bus), "DSI bus")) {
        return common::Status::ERR_IO;
    }

    esp_lcd_dbi_io_config_t dbi_cfg{};
    dbi_cfg.virtual_channel = 0;
    dbi_cfg.lcd_cmd_bits = DBI_CMD_BITS;
    dbi_cfg.lcd_param_bits = DBI_PARAM_BITS;
    if (!check(esp_lcd_new_panel_io_dbi(m_p_dsi_bus, &dbi_cfg, &m_p_io), "DBI IO")) {
        return common::Status::ERR_IO;
    }

    esp_lcd_dpi_panel_config_t dpi_cfg{};
    dpi_cfg.dpi_clk_src = MIPI_DSI_DPI_CLK_SRC_DEFAULT;
    dpi_cfg.dpi_clock_freq_mhz = m_cfg.timing.pixel_clock_mhz;
    dpi_cfg.virtual_channel = 0;
    dpi_cfg.in_color_format = LCD_COLOR_FMT_RGB565;
    dpi_cfg.num_fbs = FRAME_BUFFER_COUNT;
    dpi_cfg.video_timing.h_size = m_cfg.panel_width;
    dpi_cfg.video_timing.v_size = m_cfg.panel_height;
    dpi_cfg.video_timing.hsync_pulse_width = m_cfg.timing.hsync_pulse_width;
    dpi_cfg.video_timing.hsync_back_porch = m_cfg.timing.hsync_back_porch;
    dpi_cfg.video_timing.hsync_front_porch = m_cfg.timing.hsync_front_porch;
    dpi_cfg.video_timing.vsync_pulse_width = m_cfg.timing.vsync_pulse_width;
    dpi_cfg.video_timing.vsync_back_porch = m_cfg.timing.vsync_back_porch;
    dpi_cfg.video_timing.vsync_front_porch = m_cfg.timing.vsync_front_porch;

    jd9365_vendor_config_t vendor_cfg{};
    vendor_cfg.init_cmds = g_jd9365_vendor_init_cmds; // Chuỗi init riêng của panel 10.1" (từ BSP hãng).
    vendor_cfg.init_cmds_size = g_jd9365_vendor_init_cmds_count;
    vendor_cfg.mipi_config.dsi_bus = m_p_dsi_bus;
    vendor_cfg.mipi_config.dpi_config = &dpi_cfg;
    vendor_cfg.mipi_config.lane_num = m_cfg.dsi_lane_count;

    esp_lcd_panel_dev_config_t dev_cfg{};
    dev_cfg.reset_gpio_num = m_cfg.reset_pin;
    dev_cfg.rgb_ele_order = LCD_RGB_ELEMENT_ORDER_RGB;
    dev_cfg.bits_per_pixel = RGB565_BITS;
    dev_cfg.vendor_config = &vendor_cfg;

    if (!check(esp_lcd_new_panel_jd9365(m_p_io, &dev_cfg, &m_p_panel), "JD9365 panel") ||
        !check(esp_lcd_panel_reset(m_p_panel), "panel reset") || !check(esp_lcd_panel_init(m_p_panel), "panel init") ||
        !check(esp_lcd_panel_mirror(m_p_panel, m_cfg.scan_mirror_x, m_cfg.scan_mirror_y), "panel mirror") ||
        !check(esp_lcd_panel_disp_on_off(m_p_panel, true), "panel on")) {
        return common::Status::ERR_IO;
    }

    if (!check(esp_lcd_dpi_panel_get_frame_buffer(m_p_panel, FRAME_BUFFER_COUNT, &m_p_frame_buffer), "get FB")) {
        return common::Status::ERR_IO;
    }
    m_frame_buffer_bytes = static_cast<size_t>(m_cfg.panel_width) * m_cfg.panel_height * RGB565_BYTES;
    return common::Status::OK;
}

common::Status Jd9365Display::init_rotator()
{
    // PPA invalidate cache trên toàn bộ buffer đích -> kích thước buffer phải căn theo cache line (L1+L2).
    size_t cache_align = 0U;
    if (!check(esp_cache_get_alignment(MALLOC_CAP_SPIRAM | MALLOC_CAP_DMA, &cache_align), "cache alignment")) {
        return common::Status::ERR_IO;
    }
    if ((cache_align != 0U) && ((m_frame_buffer_bytes % cache_align) != 0U)) {
        ESP_LOGE(TAG, "FB size %u not aligned to cache line %u", static_cast<unsigned>(m_frame_buffer_bytes),
                 static_cast<unsigned>(cache_align));
        return common::Status::ERR_STATE;
    }

    ppa_client_config_t ppa_cfg{};
    ppa_cfg.oper_type = PPA_OPERATION_SRM;
    if (!check(ppa_register_client(&ppa_cfg, &m_p_ppa), "PPA client")) {
        return common::Status::ERR_IO;
    }
    ppa_event_callbacks_t callbacks{};
    callbacks.on_trans_done = &Jd9365Display::on_ppa_done;
    if (!check(ppa_client_register_event_callbacks(m_p_ppa, &callbacks), "PPA callbacks")) {
        return common::Status::ERR_IO;
    }
    ESP_LOGI(TAG, "Panel %ux%u -> logical %ux%u", static_cast<unsigned>(m_cfg.panel_width),
             static_cast<unsigned>(m_cfg.panel_height), static_cast<unsigned>(width()),
             static_cast<unsigned>(height()));
    return common::Status::OK;
}

common::Status Jd9365Display::draw_bitmap(const hal::Area& area, const uint16_t* p_pixels)
{
    return rotate(area, p_pixels, false);
}

common::Status Jd9365Display::draw_bitmap_async(const hal::Area& area, const uint16_t* p_pixels,
                                                hal::FlushDoneCallback on_done, void* p_ctx)
{
    if (on_done == nullptr) {
        return common::Status::ERR_PARAM;
    }
    m_p_done_ctx = p_ctx;
    m_on_done = on_done;
    const common::Status status = rotate(area, p_pixels, true);
    if (!common::is_ok(status)) {
        m_on_done = nullptr; // Không khởi động được: hợp đồng HAL – on_done không được gọi.
    }
    return status;
}

bool Jd9365Display::on_ppa_done(ppa_client_handle_t p_client, ppa_event_data_t* p_event, void* p_user_data)
{
    (void)p_client;
    (void)p_event;
    auto* const p_self = static_cast<Jd9365Display*>(p_user_data);
    if (p_self == nullptr) {
        return false; // IDF gọi callback cả cho phép chặn (user_data = NULL): không có gì phải báo.
    }
    const hal::FlushDoneCallback on_done = p_self->m_on_done;
    p_self->m_on_done = nullptr;
    if (on_done != nullptr) {
        on_done(p_self->m_p_done_ctx); // ISR: framework chỉ đặt cờ flush_ready.
    }
    return false; // Không đánh thức task ưu tiên cao nào.
}

common::Status Jd9365Display::rotate(const hal::Area& area, const uint16_t* p_pixels, bool is_async)
{
    if ((m_p_ppa == nullptr) || (p_pixels == nullptr)) {
        return common::Status::ERR_STATE;
    }
    if ((area.x1 < 0) || (area.y1 < 0) || (area.x2 < area.x1) || (area.y2 < area.y1) ||
        (area.x2 >= static_cast<int32_t>(width())) || (area.y2 >= static_cast<int32_t>(height()))) {
        return common::Status::ERR_PARAM;
    }
    const uint32_t block_w = static_cast<uint32_t>(area.x2 - area.x1 + 1);
    const uint32_t block_h = static_cast<uint32_t>(area.y2 - area.y1 + 1);
    const Point origin = rotation::area_origin_to_physical(m_cfg.rotation, area, static_cast<int32_t>(width()),
                                                           static_cast<int32_t>(height()));

    ppa_srm_oper_config_t op{};
    op.in.buffer = p_pixels;
    op.in.pic_w = block_w;
    op.in.pic_h = block_h;
    op.in.block_w = block_w;
    op.in.block_h = block_h;
    op.in.block_offset_x = 0U;
    op.in.block_offset_y = 0U;
    op.in.srm_cm = PPA_SRM_COLOR_MODE_RGB565;
    op.out.buffer = m_p_frame_buffer;
    op.out.buffer_size = m_frame_buffer_bytes;
    op.out.pic_w = m_cfg.panel_width;
    op.out.pic_h = m_cfg.panel_height;
    op.out.block_offset_x = static_cast<uint32_t>(origin.x);
    op.out.block_offset_y = static_cast<uint32_t>(origin.y);
    op.out.srm_cm = PPA_SRM_COLOR_MODE_RGB565;
    op.rotation_angle = to_ppa_angle(m_cfg.rotation);
    op.scale_x = PPA_SCALE_NONE;
    op.scale_y = PPA_SCALE_NONE;
    op.mirror_x = false;
    op.mirror_y = false;
    op.rgb_swap = false;
    op.byte_swap = false;
    op.mode = is_async ? PPA_TRANS_MODE_NON_BLOCKING : PPA_TRANS_MODE_BLOCKING;
    op.user_data = is_async ? this : nullptr; // IDF: chế độ blocking yêu cầu user_data = NULL.

    // ESP-IDF >= 6.1 có workaround lỗi phần cứng DIG-734 (PPA SRM treo vĩnh viễn với một số kích thước khối).
    return check(ppa_do_scale_rotate_mirror(m_p_ppa, &op), "PPA rotate") ? common::Status::OK
                                                                           : common::Status::ERR_IO;
}

} // namespace board
