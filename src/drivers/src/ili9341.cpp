/**
 * @file    ili9341.cpp
 * @brief   Hiện thực driver ILI9341.
 */
#include "drivers/ili9341.hpp"

#include <cstring>

#include "osal/osal.hpp"

namespace drivers {

namespace {

// Bảng lệnh ILI9341 (datasheet v1.11, mục 8).
constexpr uint8_t CMD_SWRESET = 0x01U;
constexpr uint8_t CMD_SLPOUT = 0x11U;
constexpr uint8_t CMD_NORON = 0x13U;
constexpr uint8_t CMD_INVOFF = 0x20U;
constexpr uint8_t CMD_INVON = 0x21U;
constexpr uint8_t CMD_DISPON = 0x29U;
constexpr uint8_t CMD_CASET = 0x2AU;
constexpr uint8_t CMD_PASET = 0x2BU;
constexpr uint8_t CMD_RAMWR = 0x2CU;
constexpr uint8_t CMD_MADCTL = 0x36U;
constexpr uint8_t CMD_COLMOD = 0x3AU;

constexpr uint8_t MADCTL_MY = 0x80U;
constexpr uint8_t MADCTL_MX = 0x40U;
constexpr uint8_t MADCTL_MV = 0x20U;
constexpr uint8_t MADCTL_BGR = 0x08U;

constexpr uint8_t COLMOD_RGB565 = 0x55U;
constexpr size_t MAX_PARAM_BYTES = 15U;
constexpr uint32_t RESET_PULSE_MS = 10U;
constexpr uint32_t RESET_WAIT_MS = 120U;
constexpr uint8_t NORON_WAIT_MS = 10U;
constexpr uint8_t DISPON_WAIT_MS = 20U;

/** @brief Một bước trong chuỗi khởi tạo. */
struct InitStep {
    uint8_t cmd;
    uint8_t len;
    uint8_t delay_ms;
    uint8_t data[MAX_PARAM_BYTES];
};

// Chuỗi power/gamma chuẩn của module ILI9341 2.4"-2.8" phổ biến.
constexpr InitStep INIT_SEQUENCE[] = {
    {0xEFU, 3U, 0U, {0x03U, 0x80U, 0x02U}},
    {0xCFU, 3U, 0U, {0x00U, 0xC1U, 0x30U}},
    {0xEDU, 4U, 0U, {0x64U, 0x03U, 0x12U, 0x81U}},
    {0xE8U, 3U, 0U, {0x85U, 0x00U, 0x78U}},
    {0xCBU, 5U, 0U, {0x39U, 0x2CU, 0x00U, 0x34U, 0x02U}},
    {0xF7U, 1U, 0U, {0x20U}},
    {0xEAU, 2U, 0U, {0x00U, 0x00U}},
    {0xC0U, 1U, 0U, {0x23U}},             // PWCTR1
    {0xC1U, 1U, 0U, {0x10U}},             // PWCTR2
    {0xC5U, 2U, 0U, {0x3EU, 0x28U}},      // VMCTR1
    {0xC7U, 1U, 0U, {0x86U}},             // VMCTR2
    {0xB1U, 2U, 0U, {0x00U, 0x18U}},      // FRMCTR1: 79 Hz
    {0xB6U, 3U, 0U, {0x08U, 0x82U, 0x27U}}, // DFUNCTR
    {0xF2U, 1U, 0U, {0x00U}},             // 3-Gamma off
    {0x26U, 1U, 0U, {0x01U}},             // Gamma curve 1
    {0xE0U, 15U, 0U, {0x0FU, 0x31U, 0x2BU, 0x0CU, 0x0EU, 0x08U, 0x4EU, 0xF1U, 0x37U, 0x07U, 0x10U, 0x03U, 0x0EU, 0x09U, 0x00U}},
    {0xE1U, 15U, 0U, {0x00U, 0x0EU, 0x14U, 0x03U, 0x11U, 0x07U, 0x31U, 0xC1U, 0x48U, 0x08U, 0x0FU, 0x0CU, 0x31U, 0x36U, 0x0FU}},
};

} // namespace

Ili9341::Ili9341(mcal::ISpiDevice& spi, mcal::IGpioOutput& dc, mcal::IGpioOutput* p_rst, const Ili9341Config& cfg)
    : m_spi(spi), m_dc(dc), m_p_rst(p_rst), m_cfg(cfg), m_is_initialized(false)
{
}

common::Status Ili9341::init()
{
    if (m_p_rst != nullptr) {
        m_p_rst->write(mcal::PinLevel::LOW);
        osal::delay_ms(RESET_PULSE_MS);
        m_p_rst->write(mcal::PinLevel::HIGH);
        osal::delay_ms(RESET_WAIT_MS);
    }

    common::Status status = send_command(CMD_SWRESET, nullptr, 0U);
    osal::delay_ms(RESET_WAIT_MS);

    const auto run = [this, &status](const InitStep* p_steps, size_t count) {
        for (size_t index = 0U; (index < count) && common::is_ok(status); ++index) {
            status = send_command(p_steps[index].cmd, p_steps[index].data, p_steps[index].len);
            if (p_steps[index].delay_ms > 0U) {
                osal::delay_ms(p_steps[index].delay_ms);
            }
        }
    };

    // Power/gamma riêng ILI9341: chỉ gửi khi chắc chắn là ILI9341 (vô nghĩa/hại với ST7789).
    if (m_cfg.vendor_init) {
        run(INIT_SEQUENCE, sizeof(INIT_SEQUENCE) / sizeof(INIT_SEQUENCE[0]));
    }

    // Lệnh MIPI DCS chuẩn, dùng chung ILI9341/ST7789. Cấu hình đặt SAU SLPOUT vì một số
    // controller bỏ qua MADCTL/COLMOD khi còn ở sleep mode.
    const InitStep dcs_sequence[] = {
        {CMD_SLPOUT, 0U, static_cast<uint8_t>(RESET_WAIT_MS), {}},
        {CMD_COLMOD, 1U, 0U, {COLMOD_RGB565}},
        {CMD_MADCTL, 1U, 0U, {build_madctl()}},
        {m_cfg.invert ? CMD_INVON : CMD_INVOFF, 0U, 0U, {}},
        {CMD_NORON, 0U, NORON_WAIT_MS, {}},
        {CMD_DISPON, 0U, DISPON_WAIT_MS, {}},
    };
    run(dcs_sequence, sizeof(dcs_sequence) / sizeof(dcs_sequence[0]));

    m_is_initialized = common::is_ok(status);
    return status;
}

common::Status Ili9341::draw_bitmap(const hal::Area& area, const uint16_t* p_pixels)
{
    if (!m_is_initialized) {
        return common::Status::ERR_STATE;
    }
    if ((p_pixels == nullptr) || (area.x1 > area.x2) || (area.y1 > area.y2) || (area.x1 < 0) || (area.y1 < 0) ||
        (area.x2 >= static_cast<int32_t>(m_cfg.width)) || (area.y2 >= static_cast<int32_t>(m_cfg.height))) {
        return common::Status::ERR_PARAM;
    }

    common::Status status = set_window(area);
    if (common::is_ok(status)) {
        const size_t pixel_count =
            static_cast<size_t>(area.x2 - area.x1 + 1) * static_cast<size_t>(area.y2 - area.y1 + 1);
        status = send_command(CMD_RAMWR, nullptr, 0U);
        if (common::is_ok(status)) {
            m_dc.write(mcal::PinLevel::HIGH);
            status = m_spi.write(reinterpret_cast<const uint8_t*>(p_pixels), pixel_count * sizeof(uint16_t));
        }
    }
    return status;
}

common::Status Ili9341::send_command(uint8_t cmd, const uint8_t* p_data, size_t len)
{
    if (len > MAX_PARAM_BYTES) {
        return common::Status::ERR_PARAM;
    }
    // Sao chép sang stack (RAM nội) – tham số có thể nằm trong flash, không DMA được.
    uint8_t buffer[MAX_PARAM_BYTES];
    const uint8_t cmd_byte = cmd;

    m_dc.write(mcal::PinLevel::LOW);
    common::Status status = m_spi.write(&cmd_byte, 1U);
    if (common::is_ok(status) && (p_data != nullptr) && (len > 0U)) {
        (void)std::memcpy(buffer, p_data, len);
        m_dc.write(mcal::PinLevel::HIGH);
        status = m_spi.write(buffer, len);
    }
    return status;
}

common::Status Ili9341::set_window(const hal::Area& area)
{
    const uint8_t col[4] = {
        static_cast<uint8_t>(static_cast<uint32_t>(area.x1) >> 8U), static_cast<uint8_t>(area.x1 & 0xFF),
        static_cast<uint8_t>(static_cast<uint32_t>(area.x2) >> 8U), static_cast<uint8_t>(area.x2 & 0xFF)};
    const uint8_t row[4] = {
        static_cast<uint8_t>(static_cast<uint32_t>(area.y1) >> 8U), static_cast<uint8_t>(area.y1 & 0xFF),
        static_cast<uint8_t>(static_cast<uint32_t>(area.y2) >> 8U), static_cast<uint8_t>(area.y2 & 0xFF)};

    common::Status status = send_command(CMD_CASET, col, sizeof(col));
    if (common::is_ok(status)) {
        status = send_command(CMD_PASET, row, sizeof(row));
    }
    return status;
}

uint8_t Ili9341::build_madctl() const
{
    uint8_t madctl = 0U;
    madctl |= m_cfg.mirror_y ? MADCTL_MY : 0U;
    madctl |= m_cfg.mirror_x ? MADCTL_MX : 0U;
    madctl |= m_cfg.swap_xy ? MADCTL_MV : 0U;
    madctl |= m_cfg.bgr ? MADCTL_BGR : 0U;
    return madctl;
}

} // namespace drivers
