/**
 * @file    frame_codec.hpp
 * @brief   [Private] Mã hóa khung cho debug bridge: COBS (không còn byte 0 trong khung) + CRC-16/CCITT.
 *
 * Trên dây: 0x00 | COBS(payload | crc16_le) | 0x00. Byte 0 ở đầu tách khung khỏi log thô đứng trước nó.
 * Cùng thuật toán với tools/gach (phía máy tính).
 */
#ifndef RUNTIME_FRAME_CODEC_HPP
#define RUNTIME_FRAME_CODEC_HPP

#include <cstddef>
#include <cstdint>

namespace runtime {
namespace codec {

constexpr uint16_t CRC_INIT = 0xFFFFU;
constexpr uint16_t CRC_POLY = 0x1021U;
constexpr uint8_t BITS_PER_BYTE = 8U;
constexpr uint16_t CRC_TOP_BIT = 0x8000U;
constexpr uint8_t COBS_MAX_RUN = 0xFFU;

/** @brief CRC-16/CCITT-FALSE. */
inline uint16_t crc16(const uint8_t* p_data, size_t len)
{
    uint16_t crc = CRC_INIT;
    for (size_t index = 0U; index < len; ++index) {
        crc = static_cast<uint16_t>(crc ^ (static_cast<uint16_t>(p_data[index]) << BITS_PER_BYTE));
        for (uint8_t bit = 0U; bit < BITS_PER_BYTE; ++bit) {
            crc = ((crc & CRC_TOP_BIT) != 0U) ? static_cast<uint16_t>((crc << 1U) ^ CRC_POLY)
                                              : static_cast<uint16_t>(crc << 1U);
        }
    }
    return crc;
}

/**
 * @brief COBS encode (thuật toán chuẩn, không kèm byte 0 kết thúc).
 * @return Số byte đầu ra, 0 nếu p_out không đủ chỗ (cần len + len/254 + 1).
 */
inline size_t cobs_encode(const uint8_t* p_in, size_t len, uint8_t* p_out, size_t out_cap)
{
    if (out_cap == 0U) {
        return 0U;
    }
    size_t code_pos = 0U;
    size_t out_pos = 1U;
    uint8_t code = 1U;
    for (size_t index = 0U; index < len; ++index) {
        const uint8_t byte = p_in[index];
        if (byte != 0U) {
            if (out_pos >= out_cap) {
                return 0U;
            }
            p_out[out_pos++] = byte;
            ++code;
        }
        if ((byte == 0U) || (code == COBS_MAX_RUN)) {
            p_out[code_pos] = code;
            code = 1U;
            code_pos = out_pos;
            const bool has_more = (index + 1U) < len;
            if ((byte == 0U) || has_more) {
                if (out_pos >= out_cap) {
                    return 0U;
                }
                ++out_pos;
            }
        }
    }
    if (code_pos < out_pos) { // Đoạn cuối vừa khép ở byte cuối cùng thì không còn ô mã nào để ghi.
        p_out[code_pos] = code;
    }
    return out_pos;
}

/**
 * @brief COBS decode (đầu vào không chứa byte 0 kết thúc).
 * @return Số byte đầu ra, 0 nếu dữ liệu hỏng hoặc p_out không đủ chỗ.
 */
inline size_t cobs_decode(const uint8_t* p_in, size_t len, uint8_t* p_out, size_t out_cap)
{
    size_t in_pos = 0U;
    size_t out_pos = 0U;
    while (in_pos < len) {
        const uint8_t code = p_in[in_pos++];
        if ((code == 0U) || ((in_pos + code - 1U) > len)) {
            return 0U;
        }
        for (uint8_t step = 1U; step < code; ++step) {
            if (out_pos >= out_cap) {
                return 0U;
            }
            p_out[out_pos++] = p_in[in_pos++];
        }
        if ((code != COBS_MAX_RUN) && (in_pos < len)) {
            if (out_pos >= out_cap) {
                return 0U;
            }
            p_out[out_pos++] = 0U;
        }
    }
    return out_pos;
}

} // namespace codec
} // namespace runtime

#endif /* RUNTIME_FRAME_CODEC_HPP */
