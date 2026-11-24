/**
 * @file    diag.cpp
 * @brief   Backend của common::trace / common::event và sổ lỗi bền (NVS).
 */
#include "diag.hpp"

#include <atomic>
#include <cstdio>
#include <cstring>

#include "common/string_utils.hpp"
#include "hal_api/board.hpp"
#include "osal/osal.hpp"
#include "runtime/runtime.hpp"

namespace runtime {

namespace {

constexpr const char* FAULT_KEY = "faults";
constexpr size_t FAULT_BLOB_LEN = diag::FAULT_CAPACITY * diag::FAULT_TEXT_LEN;
constexpr uint32_t MS_PER_S = 1000U;
constexpr size_t FAULT_BUILD_LEN = 192U;

// ---- Trace: ghi không khóa (chỉ số atomic), dừng khi đầy ----
diag::TraceRecord s_trace[diag::TRACE_CAPACITY];
std::atomic<uint32_t> s_trace_next{0U};
std::atomic<uint32_t> s_trace_dropped{0U};

void trace_sink(common::trace::Phase phase, const char* p_cat, const char* p_name, int32_t value)
{
    const uint32_t index = s_trace_next.fetch_add(1U, std::memory_order_relaxed);
    if (index >= diag::TRACE_CAPACITY) {
        s_trace_dropped.fetch_add(1U, std::memory_order_relaxed);
        return;
    }
    diag::TraceRecord& record = s_trace[index];
    record.ts_us = osal::time_us();
    record.p_cat = p_cat;
    record.p_name = p_name;
    record.value = value;
    record.p_task = osal::current_task_name();
    record.phase = static_cast<uint8_t>(phase);
    record.core = osal::current_core();
}

// ---- Sự kiện ----
diag::EventRecord s_events[diag::EVENT_CAPACITY];
uint32_t s_event_seq = 0U;

osal::Mutex& event_lock()
{
    static osal::Mutex s_mutex; // Tạo khi có sự kiện đầu tiên (RTOS đã chạy).
    return s_mutex;
}

void event_sink(common::event::Severity severity, const char* p_source, const char* p_text)
{
    osal::LockGuard guard(event_lock());
    const uint32_t seq = ++s_event_seq;
    diag::EventRecord& record = s_events[(seq - 1U) % diag::EVENT_CAPACITY];
    record.seq = seq;
    record.ts_ms = osal::tick_ms();
    record.severity = severity;
    record.p_source = p_source;
    (void)common::copy_string(record.text, sizeof(record.text), p_text);
}

// ---- Lỗi (NVS) ----
hal::IKeyValueStore* s_p_store = nullptr;
uint32_t s_boot_count = 0U;
char s_faults[diag::FAULT_CAPACITY][diag::FAULT_TEXT_LEN];
size_t s_fault_count = 0U;

void save_faults()
{
    if (s_p_store == nullptr) {
        return;
    }
    static char s_blob[FAULT_BLOB_LEN];
    size_t used = 0U;
    for (size_t index = 0U; index < s_fault_count; ++index) {
        used += common::copy_string(&s_blob[used], sizeof(s_blob) - used, s_faults[index]);
        used += common::copy_string(&s_blob[used], sizeof(s_blob) - used, "\n");
    }
    s_blob[(used < sizeof(s_blob)) ? used : (sizeof(s_blob) - 1U)] = '\0';
    (void)s_p_store->set_string(FAULT_KEY, s_blob);
}

void load_faults()
{
    s_fault_count = 0U;
    static char s_blob[FAULT_BLOB_LEN];
    if ((s_p_store == nullptr) || !common::is_ok(s_p_store->get_string(FAULT_KEY, s_blob, sizeof(s_blob)))) {
        return;
    }
    char* p_save = nullptr;
    for (char* p_line = strtok_r(s_blob, "\n", &p_save); (p_line != nullptr) && (s_fault_count < diag::FAULT_CAPACITY);
         p_line = strtok_r(nullptr, "\n", &p_save)) {
        (void)common::copy_string(s_faults[s_fault_count++], diag::FAULT_TEXT_LEN, p_line);
    }
}

const char* reset_text(hal::ResetReason reason)
{
    switch (reason) {
    case hal::ResetReason::PANIC: return "panic / crash";
    case hal::ResetReason::INT_WATCHDOG: return "interrupt watchdog";
    case hal::ResetReason::TASK_WATCHDOG: return "task watchdog";
    case hal::ResetReason::OTHER_WATCHDOG: return "watchdog";
    case hal::ResetReason::BROWNOUT: return "brownout (supply voltage dropped)";
    case hal::ResetReason::POWER_GLITCH: return "power glitch";
    case hal::ResetReason::CPU_LOCKUP: return "CPU lockup";
    default: return nullptr;
    }
}

} // namespace

void diag_install()
{
    common::trace::set_sink(&trace_sink);
    common::event::set_sink(&event_sink);
}

void diag_boot(const hal::Devices& devices)
{
    s_p_store = devices.p_storage;
    load_faults();
    if (devices.p_system == nullptr) {
        return;
    }
    hal::CrashSummary crash{};
    const bool has_crash = common::is_ok(devices.p_system->crash_summary(crash));
    if (has_crash) {
        EVENT_W("system", "crash dump stored: task %s, pc 0x%08lx, %s (gach crash info)", crash.task,
                static_cast<unsigned long>(crash.pc), crash.reason);
    }
    hal::BootInfo boot{};
    if (!common::is_ok(devices.p_system->get_boot_info(boot))) {
        return;
    }
    s_boot_count = boot.boot_count;
    const char* const p_reset = reset_text(boot.reason);
    if (p_reset == nullptr) {
        return;
    }
    EVENT_E("system", "abnormal reset: %s", p_reset);
    // Dump vẫn nằm trong flash các lần boot sau: chỉ gắn vào lỗi của lần reset do chính nó gây ra.
    char text[FAULT_BUILD_LEN]; // Đủ chỗ cho mọi trường; fault_record cắt về FAULT_TEXT_LEN.
    if (has_crash && (boot.reason == hal::ResetReason::PANIC)) {
        (void)std::snprintf(text, sizeof(text), "%s: %s in task %s, pc 0x%08lx", p_reset, crash.reason, crash.task,
                            static_cast<unsigned long>(crash.pc));
    } else {
        (void)common::copy_string(text, sizeof(text), p_reset);
    }
    fault_record("reset", text);
}

void fault_record(const char* p_code, const char* p_text)
{
    if (s_fault_count == diag::FAULT_CAPACITY) { // Đầy: bỏ lỗi cũ nhất.
        std::memmove(&s_faults[0], &s_faults[1], sizeof(s_faults[0]) * (diag::FAULT_CAPACITY - 1U));
        --s_fault_count;
    }
    (void)std::snprintf(s_faults[s_fault_count++], diag::FAULT_TEXT_LEN, "%u|%u|%s|%s",
                        static_cast<unsigned>(s_boot_count), static_cast<unsigned>(osal::tick_ms() / MS_PER_S),
                        (p_code != nullptr) ? p_code : "?", (p_text != nullptr) ? p_text : "");
    save_faults();
}

namespace diag {

void trace_start()
{
    common::trace::set_enabled(false);
    s_trace_next.store(0U);
    s_trace_dropped.store(0U);
    common::trace::set_enabled(true);
}

void trace_stop()
{
    common::trace::set_enabled(false);
}

bool trace_running()
{
    return common::trace::is_enabled();
}

size_t trace_count()
{
    const uint32_t next = s_trace_next.load();
    return (next < TRACE_CAPACITY) ? next : TRACE_CAPACITY;
}

uint32_t trace_dropped()
{
    return s_trace_dropped.load();
}

const TraceRecord& trace_at(size_t index)
{
    return s_trace[index];
}

size_t events_since(uint32_t after, EventRecord* p_out, size_t max)
{
    osal::LockGuard guard(event_lock());
    const uint32_t oldest = (s_event_seq > EVENT_CAPACITY) ? (s_event_seq - EVENT_CAPACITY + 1U) : 1U;
    uint32_t seq = (after + 1U > oldest) ? (after + 1U) : oldest;
    size_t count = 0U;
    for (; (seq <= s_event_seq) && (count < max); ++seq) {
        p_out[count++] = s_events[(seq - 1U) % EVENT_CAPACITY];
    }
    return count;
}

uint32_t events_last_seq()
{
    osal::LockGuard guard(event_lock());
    return s_event_seq;
}

const char* fault_at(size_t index)
{
    return (index < s_fault_count) ? s_faults[index] : "";
}

size_t fault_count()
{
    return s_fault_count;
}

void faults_clear()
{
    s_fault_count = 0U;
    save_faults();
}

} // namespace diag
} // namespace runtime
