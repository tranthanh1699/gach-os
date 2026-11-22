/**
 * @file    wifi.hpp
 * @brief   HAL contract: Wi-Fi station (quét, kết nối) – mô hình bất đồng bộ + polling.
 *
 * Mọi thao tác dài (scan, connect) trả về ngay; tầng trên đọc trạng thái định kỳ.
 * Hiện thực phải thread-safe: callback của stack mạng chạy ở task khác với UI task.
 */
#ifndef HAL_API_WIFI_HPP
#define HAL_API_WIFI_HPP

#include <cstddef>
#include <cstdint>

#include "common/status.hpp"

namespace hal {

constexpr size_t WIFI_SSID_MAX_LEN = 32U;     /**< Độ dài SSID tối đa (IEEE 802.11). */
constexpr size_t WIFI_PASSWORD_MAX_LEN = 64U; /**< Độ dài passphrase WPA2 tối đa. */

/** @brief Trạng thái kết nối station. */
enum class WifiState : uint8_t {
    DISCONNECTED, /**< Chưa kết nối / đã ngắt chủ động. */
    CONNECTING,   /**< Đang kết nối hoặc đang thử lại. */
    CONNECTED,    /**< Đã có địa chỉ IP. */
    FAILED        /**< Hết số lần thử (sai mật khẩu, không thấy AP...). */
};

/** @brief Một access point tìm thấy khi quét. */
struct WifiAp {
    char ssid[WIFI_SSID_MAX_LEN + 1U];
    int8_t rssi; /**< dBm. */
    bool secure; /**< true nếu cần mật khẩu. */
};

/** @brief Wi-Fi station. */
class IWifi {
public:
    virtual ~IWifi() = default;

    /** @brief Bắt đầu quét (không chặn). @return ERR_STATE nếu đang quét/đang kết nối. */
    virtual common::Status start_scan() = 0;

    /** @return true khi đang quét. */
    virtual bool is_scanning() const = 0;

    /** @return Bộ đếm tăng mỗi lần quét xong – dùng để phát hiện có kết quả mới. */
    virtual uint32_t scan_generation() const = 0;

    /**
     * @brief Chép kết quả quét gần nhất (sắp xếp theo RSSI giảm dần).
     * @param[out] p_out     Mảng đích.
     * @param[in]  max_count Số phần tử tối đa của mảng.
     * @return Số AP đã chép.
     */
    virtual size_t scan_results(WifiAp* p_out, size_t max_count) const = 0;

    /**
     * @brief Bắt đầu kết nối (không chặn). Ngắt kết nối hiện tại nếu có.
     * @param[in] p_ssid     SSID (<= WIFI_SSID_MAX_LEN).
     * @param[in] p_password Mật khẩu, "" cho mạng mở.
     */
    virtual common::Status connect(const char* p_ssid, const char* p_password) = 0;

    /** @brief Ngắt kết nối chủ động (không tự kết nối lại). */
    virtual common::Status disconnect() = 0;

    /** @return Trạng thái kết nối hiện tại. */
    virtual WifiState state() const = 0;

    /**
     * @brief Địa chỉ IPv4 dạng chuỗi "a.b.c.d".
     * @return false nếu chưa có IP.
     */
    virtual bool ip_address(char* p_buf, size_t buf_len) const = 0;
};

} // namespace hal

#endif /* HAL_API_WIFI_HPP */
