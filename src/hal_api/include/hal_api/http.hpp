/**
 * @file    http.hpp
 * @brief   HAL contract: HTTP(S) GET bất đồng bộ (một yêu cầu tại một thời điểm).
 *
 * Mô hình polling để không ràng buộc ngữ cảnh gọi: start_get() trả về ngay, yêu cầu chạy trên task riêng của
 * platform; người dùng hỏi state() định kỳ, khi DONE thì đọc result()/body() rồi release() để nhận yêu cầu mới.
 * Bộ đệm phản hồi thuộc về platform và giữ nguyên tới lần release() kế tiếp.
 */
#ifndef HAL_API_HTTP_HPP
#define HAL_API_HTTP_HPP

#include <cstddef>
#include <cstdint>

#include "common/status.hpp"

namespace hal {

constexpr size_t HTTP_URL_MAX_LEN = 512U; /**< Kể cả '\0'. */

/** @brief Trạng thái của kênh yêu cầu duy nhất. */
enum class HttpState : uint8_t {
    IDLE,    /**< Sẵn sàng nhận yêu cầu. */
    RUNNING, /**< Đang kết nối / nhận dữ liệu. */
    DONE     /**< Có kết quả; gọi release() để về IDLE. */
};

/** @brief Kết quả một yêu cầu. */
struct HttpResult {
    common::Status status; /**< OK = có phản hồi HTTP (kể cả mã 4xx/5xx); khác = lỗi mạng/TLS/timeout. */
    int32_t http_code;     /**< Mã trạng thái HTTP (0 nếu không có phản hồi). */
    size_t body_len;       /**< Số byte trong body(). */
    bool truncated;        /**< Phản hồi dài hơn bộ đệm: body() chỉ là phần đầu. */
};

/** @brief Máy khách HTTP(S) do platform cung cấp. */
class IHttpClient {
public:
    virtual ~IHttpClient() = default;

    /**
     * @brief Bắt đầu GET (http:// hoặc https://, chứng chỉ máy chủ được kiểm tra).
     * @return ERR_STATE nếu chưa ở IDLE, ERR_PARAM nếu URL rỗng/quá dài.
     */
    virtual common::Status start_get(const char* p_url) = 0;

    /** @return Trạng thái hiện tại (an toàn gọi từ mọi task). */
    virtual HttpState state() const = 0;

    /** @return Kết quả; chỉ có nghĩa khi state() == DONE. */
    virtual HttpResult result() const = 0;

    /** @return Body phản hồi (không kết thúc '\0'); chỉ có nghĩa khi state() == DONE. */
    virtual const uint8_t* body() const = 0;

    /** @return Dung lượng bộ đệm phản hồi (byte). */
    virtual size_t capacity() const = 0;

    /** @brief DONE -> IDLE (bỏ kết quả). Không làm gì ở trạng thái khác. */
    virtual void release() = 0;
};

} // namespace hal

#endif /* HAL_API_HTTP_HPP */
