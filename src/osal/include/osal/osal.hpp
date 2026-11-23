/**
 * @file    osal.hpp
 * @brief   OS Abstraction Layer: task, mutex, thời gian. Không lộ kiểu dữ liệu của RTOS.
 *
 * @note    Port mặc định: FreeRTOS (`port/freertos`). Port mới chỉ cần hiện thực file này.
 */
#ifndef OSAL_OSAL_HPP
#define OSAL_OSAL_HPP

#include <cstdint>

#include "common/non_copyable.hpp"
#include "common/status.hpp"

namespace osal {

/** @brief Timeout vô hạn cho các API chờ. */
constexpr uint32_t WAIT_FOREVER = UINT32_MAX;

/** @brief Không ghim task vào core nào (để scheduler tự chọn). */
constexpr int8_t CORE_ANY = -1;

/** @return Số mili-giây kể từ khi scheduler chạy (wrap-around sau ~49 ngày). */
uint32_t tick_ms();

/** @return Số micro-giây kể từ khi khởi động (đo hiệu năng; wrap-around sau ~71 phút). */
uint32_t time_us();

/**
 * @brief Nhường CPU cho task khác trong khoảng thời gian chỉ định.
 * @param[in] ms Thời gian chờ (ms). 0 = chỉ yield.
 */
void delay_ms(uint32_t ms);

/** @brief Mutex đệ quy (recursive) – an toàn khi cùng task lock lồng nhau. */
class Mutex : private common::NonCopyable {
public:
    Mutex();
    ~Mutex();

    /**
     * @brief Khóa mutex.
     * @param[in] timeout_ms Thời gian chờ tối đa (ms) hoặc WAIT_FOREVER.
     * @return true nếu lấy được khóa.
     */
    bool lock(uint32_t timeout_ms = WAIT_FOREVER);

    /** @brief Mở khóa (chỉ task đang giữ khóa được gọi). */
    void unlock();

private:
    void* m_handle; /**< Handle RTOS opaque, không lộ kiểu ra ngoài. */
};

/** @brief RAII lock cho osal::Mutex. */
class LockGuard : private common::NonCopyable {
public:
    explicit LockGuard(Mutex& mutex) : m_mutex(mutex) { (void)m_mutex.lock(); }
    ~LockGuard() { m_mutex.unlock(); }

private:
    Mutex& m_mutex;
};

/** @brief Semaphore nhị phân – đồng bộ 1-1 giữa hai task (VD: chờ task khác khởi tạo xong). */
class BinarySemaphore : private common::NonCopyable {
public:
    BinarySemaphore();
    ~BinarySemaphore();

    /** @brief Báo hiệu (an toàn gọi từ task, không gọi từ ISR). */
    void give();

    /**
     * @brief Chờ tín hiệu.
     * @param[in] timeout_ms Thời gian chờ tối đa (ms) hoặc WAIT_FOREVER.
     * @return true nếu nhận được tín hiệu trước khi hết hạn.
     */
    bool take(uint32_t timeout_ms = WAIT_FOREVER);

private:
    void* m_handle;
};

/** @brief Hàm entry của task. */
using TaskEntry = void (*)(void* p_arg);

/** @brief Cấu hình tạo task. */
struct TaskConfig {
    const char* p_name;   /**< Tên task (debug). */
    uint32_t stack_bytes; /**< Kích thước stack tính bằng byte. */
    uint8_t priority;     /**< Độ ưu tiên (lớn hơn = ưu tiên cao hơn). */
    int8_t core_id;       /**< Core ghim task, hoặc CORE_ANY. Core không tồn tại trên chip -> như CORE_ANY. */
};

/**
 * @brief Tạo task mới. Chỉ gọi trong pha khởi tạo hệ thống.
 *
 * @param[in] config Cấu hình task.
 * @param[in] entry  Hàm entry (không được return).
 * @param[in] p_arg  Tham số truyền cho entry.
 * @return common::Status::OK hoặc ERR_NO_MEM.
 */
common::Status task_create(const TaskConfig& config, TaskEntry entry, void* p_arg);

/** @brief Task hiện tại tự kết thúc và giải phóng tài nguyên (không return). */
[[noreturn]] void task_exit();

} // namespace osal

#endif /* OSAL_OSAL_HPP */
