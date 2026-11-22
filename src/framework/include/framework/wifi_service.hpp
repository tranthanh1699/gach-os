/**
 * @file    wifi_service.hpp
 * @brief   Dịch vụ Wi-Fi cho app: quét, kết nối, nhớ mạng, tự kết nối lại khi khởi động.
 *
 * @note    Chỉ dùng trong UI task. Trạng thái được cập nhật bởi framework mỗi config::NET_POLL_PERIOD_MS.
 */
#ifndef FRAMEWORK_WIFI_SERVICE_HPP
#define FRAMEWORK_WIFI_SERVICE_HPP

#include <cstddef>
#include <cstdint>

#include "common/non_copyable.hpp"
#include "common/status.hpp"
#include "framework/config.hpp"

namespace hal {
struct Devices;
struct WifiAp;
} // namespace hal

namespace fw {

constexpr size_t WIFI_SSID_BUF_LEN = 33U;     /**< 32 ký tự + '\0'. */
constexpr size_t WIFI_PASSWORD_BUF_LEN = 65U; /**< 64 ký tự + '\0'. */
constexpr size_t IP_ADDRESS_BUF_LEN = 16U;    /**< "255.255.255.255" + '\0'. */

/** @brief Trạng thái Wi-Fi hiển thị cho app. */
enum class WifiStatus : uint8_t {
    UNAVAILABLE,  /**< Board không có Wi-Fi. */
    DISCONNECTED,
    CONNECTING,
    CONNECTED,
    FAILED        /**< Sai mật khẩu / không tìm thấy mạng. */
};

/** @brief Một mạng tìm thấy (đã lọc trùng SSID, giữ RSSI mạnh nhất). */
struct WifiNetwork {
    char ssid[WIFI_SSID_BUF_LEN];
    int8_t rssi;
    bool secure;
};

/** @brief Dịch vụ Wi-Fi. */
class WifiService : private common::NonCopyable {
public:
    WifiService();

    /** @return true nếu board có Wi-Fi. */
    bool is_available() const;

    /** @return Trạng thái hiện tại. */
    WifiStatus status() const { return m_status; }

    /** @brief Quét mạng (không chặn). Kết quả có khi scan_generation() thay đổi. */
    common::Status scan();

    /** @return true khi đang quét. */
    bool is_scanning() const;

    /** @return Bộ đếm tăng mỗi khi danh sách mạng được cập nhật. */
    uint32_t scan_generation() const { return m_scan_generation; }

    /** @return Số mạng trong danh sách. */
    size_t network_count() const { return m_network_count; }

    /** @return Mạng thứ `index`, NULL nếu ngoài dải. */
    const WifiNetwork* network_at(size_t index) const;

    /**
     * @brief Kết nối (không chặn).
     * @param[in] p_ssid     SSID.
     * @param[in] p_password Mật khẩu ("" cho mạng mở).
     * @param[in] remember   Lưu vào bộ nhớ bền khi kết nối THÀNH CÔNG để tự kết nối lần sau.
     */
    common::Status connect(const char* p_ssid, const char* p_password, bool remember);

    /** @brief Ngắt kết nối. @param[in] forget Xóa mạng đã lưu. */
    common::Status disconnect(bool forget);

    /** @return SSID đang kết nối / đang thử ("" nếu không có). */
    const char* current_ssid() const { return m_ssid; }

    /** @brief Địa chỉ IP hiện tại. @return false nếu chưa có. */
    bool ip_address(char* p_buf, size_t buf_len) const;

    /** @brief [Nội bộ framework] Gắn thiết bị. */
    void bind(const hal::Devices& devices) { m_p_devices = &devices; }

    /** @brief [Nội bộ framework] Tự kết nối mạng đã lưu (nếu có). */
    void start();

    /**
     * @brief [Nội bộ framework] Đồng bộ trạng thái từ HAL. Gọi định kỳ trong UI task.
     * @return true đúng một lần tại thời điểm vừa kết nối thành công (cạnh lên).
     */
    bool poll();

private:
    void refresh_networks();
    void save_credentials();

    const hal::Devices* m_p_devices;
    WifiStatus m_status;
    WifiNetwork m_networks[config::WIFI_MAX_NETWORKS];
    size_t m_network_count;
    uint32_t m_hal_scan_generation;
    uint32_t m_scan_generation;
    char m_ssid[WIFI_SSID_BUF_LEN];
    char m_pending_password[WIFI_PASSWORD_BUF_LEN];
    bool m_remember_pending;
};

} // namespace fw

#endif /* FRAMEWORK_WIFI_SERVICE_HPP */
