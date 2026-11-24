/**
 * @file    diag.hpp
 * @brief   [Private] Chẩn đoán: bộ đệm trace, sổ sự kiện, sổ lỗi (fault) bền qua khởi động lại.
 */
#ifndef RUNTIME_DIAG_HPP
#define RUNTIME_DIAG_HPP

#include <cstddef>
#include <cstdint>

#include "common/event.hpp"
#include "common/trace.hpp"

namespace runtime {
namespace diag {

constexpr size_t TRACE_CAPACITY = 8192U;
constexpr size_t EVENT_CAPACITY = 256U;
constexpr size_t FAULT_CAPACITY = 16U;
constexpr size_t FAULT_TEXT_LEN = 112U;

/** @brief Một bản ghi trace. */
struct TraceRecord {
    uint32_t ts_us;
    const char* p_cat;
    const char* p_name;
    const char* p_task; /**< Tên task (con trỏ của RTOS). */
    int32_t value;
    uint8_t phase; /**< common::trace::Phase */
    uint8_t core;
};

/** @brief Một sự kiện hệ thống. */
struct EventRecord {
    uint32_t seq;   /**< Tăng dần từ 1 (để đọc tiếp từ chỗ đã đọc). */
    uint32_t ts_ms;
    common::event::Severity severity;
    const char* p_source;
    char text[common::event::TEXT_MAX_LEN + 1U];
};

// ---- Trace ----
void trace_start();           /**< Xóa bộ đệm và bắt đầu ghi (tới khi đầy / dừng). */
void trace_stop();
bool trace_running();
size_t trace_count();         /**< Số bản ghi đã lưu. */
uint32_t trace_dropped();     /**< Bản ghi bị bỏ vì đầy. */
const TraceRecord& trace_at(size_t index);

// ---- Sự kiện ----
/** @brief Chép sự kiện có seq > after (tối đa max, cũ -> mới). @return số sự kiện chép. */
size_t events_since(uint32_t after, EventRecord* p_out, size_t max);
uint32_t events_last_seq();

// ---- Lỗi ----
/** @brief Lỗi thứ index (0 = cũ nhất), dạng "boot#|uptime_s|code|text". */
const char* fault_at(size_t index);
size_t fault_count();
void faults_clear();

} // namespace diag
} // namespace runtime

#endif /* RUNTIME_DIAG_HPP */
