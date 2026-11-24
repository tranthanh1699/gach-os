/**
 * @file    bridge_diag.cpp
 * @brief   Lệnh chẩn đoán của debug bridge: log (xem / lọc), trace, event, fault, crash, watchdog.
 *
 * Chạy trên task bridge (stack nhỏ): mảng lớn là biến tĩnh (runtime .bss nằm trong PSRAM).
 */
#include <cctype>
#include <cstdarg>
#include <cstdint>
#include <cstdio>
#include <cstdlib>
#include <cstring>

#include "bridge_sys.hpp"
#include "diag.hpp"
#include "fs_utils.hpp"
#include "osal/osal.hpp"

namespace runtime {

namespace {

constexpr size_t LINE_LEN = 200U;
constexpr size_t PACK_LEN = 230U;          /**< Một khung TEXT gói nhiều bản ghi trace (MAX_DATA 256). */
constexpr size_t LOG_DEFAULT_LINES = 50U;
constexpr size_t EVENT_DEFAULT = 30U;
constexpr size_t EVENT_BATCH = 16U;
constexpr size_t TRACE_STRINGS_MAX = 192U; /**< Chuỗi khác nhau (cat|name, tên task) trong một lần dump. */
constexpr size_t CRASH_CHUNK = 4096U;
constexpr uint32_t MS_PER_S = 1000U;
constexpr uint32_t CRASH_TEST_DELAY_MS = 200U;
constexpr const char* CRASH_DIR = "crash";
constexpr const char* CRASH_FILE = "crash/coredump.bin";

void line(uint8_t id, const char* p_fmt, ...) __attribute__((format(printf, 2, 3)));
void line(uint8_t id, const char* p_fmt, ...)
{
    char text[LINE_LEN];
    va_list args;
    va_start(args, p_fmt);
    (void)std::vsnprintf(text, sizeof(text), p_fmt, args);
    va_end(args);
    bridge_text(id, text);
}

bool unsupported(uint8_t id, common::Status status)
{
    if (common::is_ok(status)) {
        return false;
    }
    bridge_resp(id, false, (status == common::Status::ERR_NOT_SUPPORTED) ? "not supported on this platform" : "failed");
    return true;
}

// ============================================================================
// log [n] | log filter <text> [-l e|w|i|d|v]
// ============================================================================
constexpr const char* LEVEL_ORDER = "EWIDV";

/** @brief Mức của dòng log (ký tự đầu "E (123) tag: ..."); dòng không theo định dạng coi là INFO. */
size_t line_rank(const char* p_line)
{
    const char* const p_hit = ((p_line[0] != '\0') && (p_line[1] == ' ')) ? std::strchr(LEVEL_ORDER, p_line[0]) : nullptr;
    return (p_hit != nullptr) ? static_cast<size_t>(p_hit - LEVEL_ORDER) : 2U;
}

struct LogQuery {
    uint8_t id;
    const char* p_text;  /**< NULL = không lọc chữ. */
    size_t max_rank;     /**< Chỉ dòng có mức <= (E=0 ... V=4). */
    size_t skip;         /**< Bỏ qua n dòng khớp đầu tiên (để in n dòng cuối). */
    size_t matched;
};

bool log_matches(const char* p_line, const LogQuery& query)
{
    return (line_rank(p_line) <= query.max_rank) && ((query.p_text == nullptr) || (std::strstr(p_line, query.p_text) != nullptr));
}

void cmd_log(uint8_t id, size_t argc, char* const p_args[])
{
    LogQuery query{id, nullptr, 4U, 0U, 0U};
    size_t want = LOG_DEFAULT_LINES;
    size_t arg = 1U;
    if ((argc > 1U) && (std::strcmp(p_args[1], "filter") == 0)) {
        if (argc < 3U) {
            bridge_resp(id, false, "usage: log filter <tag|text> [-l e|w|i|d|v] [-n N]");
            return;
        }
        query.p_text = p_args[2];
        want = SIZE_MAX;
        arg = 3U;
    }
    for (; arg < argc; ++arg) {
        const char* const p_next = (arg + 1U < argc) ? p_args[arg + 1U] : nullptr;
        if ((std::strcmp(p_args[arg], "-l") == 0) && (p_next != nullptr)) {
            const char* const p_hit = std::strchr(LEVEL_ORDER, std::toupper(static_cast<unsigned char>(p_next[0])));
            if ((p_hit == nullptr) || (p_next[0] == '\0')) {
                bridge_resp(id, false, "level must be e, w, i, d or v");
                return;
            }
            query.max_rank = static_cast<size_t>(p_hit - LEVEL_ORDER);
            ++arg;
        } else if ((std::strcmp(p_args[arg], "-n") == 0) && (p_next != nullptr)) {
            want = std::strtoul(p_next, nullptr, 10);
            ++arg;
        } else {
            want = std::strtoul(p_args[arg], nullptr, 10);
        }
    }
    // Lần 1: đếm dòng khớp; lần 2: in `want` dòng cuối.
    bridge_for_each_log_line([](const char* p_line, void* p_ctx) {
        auto& q = *static_cast<LogQuery*>(p_ctx);
        q.matched += log_matches(p_line, q) ? 1U : 0U;
    }, &query);
    const size_t total = query.matched;
    query.skip = (want < total) ? (total - want) : 0U;
    query.matched = 0U;
    bridge_for_each_log_line([](const char* p_line, void* p_ctx) {
        auto& q = *static_cast<LogQuery*>(p_ctx);
        if (log_matches(p_line, q) && (q.matched++ >= q.skip)) {
            bridge_text(q.id, p_line);
        }
    }, &query);
    char text[48];
    (void)std::snprintf(text, sizeof(text), "%u of %u matching lines", static_cast<unsigned>(total - query.skip),
                        static_cast<unsigned>(total));
    bridge_resp(id, true, text);
}

// ============================================================================
// trace start|stop|status|dump
// ============================================================================
// Định dạng dump (tools/gach chuyển sang JSON Chrome / Perfetto):
//   "S <idx> <chuỗi>"                              bảng chuỗi (cat|name, tên task)
//   "R <tok> <tok> ..."  tok = dt.ph.core.task.str[.value]  (hex; dt = µs so với bản ghi trước, value thập phân)
const char* s_strings[TRACE_STRINGS_MAX];
const char* s_strings2[TRACE_STRINGS_MAX]; /**< name của cặp (cat, name); NULL = chuỗi đơn (tên task). */
size_t s_string_count = 0U;

/** @return Chỉ số chuỗi (thêm vào bảng + gửi dòng "S" nếu mới); TRACE_STRINGS_MAX nếu bảng đầy. */
size_t intern(uint8_t id, const char* p_first, const char* p_second)
{
    for (size_t index = 0U; index < s_string_count; ++index) {
        if ((s_strings[index] == p_first) && (s_strings2[index] == p_second)) {
            return index;
        }
    }
    if (s_string_count == TRACE_STRINGS_MAX) {
        return TRACE_STRINGS_MAX;
    }
    s_strings[s_string_count] = p_first;
    s_strings2[s_string_count] = p_second;
    if (p_second != nullptr) {
        line(id, "S %x %s|%s", static_cast<unsigned>(s_string_count), p_first, p_second);
    } else {
        line(id, "S %x %.16s", static_cast<unsigned>(s_string_count), (p_first != nullptr) ? p_first : "?");
    }
    return s_string_count++;
}

void cmd_trace_dump(uint8_t id)
{
    if (diag::trace_running()) {
        bridge_resp(id, false, "stop the trace first (trace stop)");
        return;
    }
    const size_t count = diag::trace_count();
    s_string_count = 0U;
    char pack[PACK_LEN + 1U];
    size_t used = 0U;
    uint32_t prev_us = (count > 0U) ? diag::trace_at(0U).ts_us : 0U;
    line(id, "T %lx %u", static_cast<unsigned long>(prev_us), static_cast<unsigned>(count)); // Mốc thời gian đầu.
    for (size_t index = 0U; index < count; ++index) {
        const diag::TraceRecord& rec = diag::trace_at(index);
        const size_t str = intern(id, rec.p_cat, rec.p_name);
        const size_t task = intern(id, rec.p_task, nullptr);
        char tok[48];
        int len = std::snprintf(tok, sizeof(tok), " %lx.%x.%x.%x.%x", static_cast<unsigned long>(rec.ts_us - prev_us),
                                static_cast<unsigned>(rec.phase), static_cast<unsigned>(rec.core),
                                static_cast<unsigned>(task), static_cast<unsigned>(str));
        if (rec.value != 0) {
            len += std::snprintf(&tok[len], sizeof(tok) - static_cast<size_t>(len), ".%ld", static_cast<long>(rec.value));
        }
        prev_us = rec.ts_us;
        if (used + static_cast<size_t>(len) > PACK_LEN) {
            bridge_text(id, pack);
            used = 0U;
        }
        if (used == 0U) {
            pack[0] = 'R';
            used = 1U;
        }
        (void)std::memcpy(&pack[used], tok, static_cast<size_t>(len) + 1U);
        used += static_cast<size_t>(len);
    }
    if (used > 0U) {
        bridge_text(id, pack);
    }
    char text[64];
    (void)std::snprintf(text, sizeof(text), "%u records, %u dropped", static_cast<unsigned>(count),
                        static_cast<unsigned>(diag::trace_dropped()));
    bridge_resp(id, true, text);
}

void cmd_trace(uint8_t id, const char* p_sub)
{
    char text[96];
    if ((p_sub != nullptr) && (std::strcmp(p_sub, "start") == 0)) {
        diag::trace_start();
        (void)std::snprintf(text, sizeof(text), "tracing (buffer %u records, stops when full)",
                            static_cast<unsigned>(diag::TRACE_CAPACITY));
        bridge_resp(id, true, text);
    } else if ((p_sub != nullptr) && (std::strcmp(p_sub, "stop") == 0)) {
        diag::trace_stop();
        (void)std::snprintf(text, sizeof(text), "stopped: %u records, %u dropped",
                            static_cast<unsigned>(diag::trace_count()), static_cast<unsigned>(diag::trace_dropped()));
        bridge_resp(id, true, text);
    } else if ((p_sub != nullptr) && (std::strcmp(p_sub, "dump") == 0)) {
        cmd_trace_dump(id);
    } else if ((p_sub == nullptr) || (std::strcmp(p_sub, "status") == 0)) {
        (void)std::snprintf(text, sizeof(text), "%s, %u/%u records, %u dropped",
                            diag::trace_running() ? "running" : "stopped", static_cast<unsigned>(diag::trace_count()),
                            static_cast<unsigned>(diag::TRACE_CAPACITY), static_cast<unsigned>(diag::trace_dropped()));
        bridge_resp(id, true, text);
    } else {
        bridge_resp(id, false, "usage: trace start|stop|status|dump");
    }
}

// ============================================================================
// event list [n] | event since <seq>
// ============================================================================
diag::EventRecord s_event_batch[EVENT_BATCH];

void cmd_event(uint8_t id, const char* p_sub, const char* p_arg)
{
    static const char SEVERITY[] = {'I', 'W', 'E'};
    uint32_t after = 0U;
    if ((p_sub != nullptr) && (std::strcmp(p_sub, "since") == 0)) {
        after = (p_arg != nullptr) ? static_cast<uint32_t>(std::strtoul(p_arg, nullptr, 10)) : 0U;
    } else if ((p_sub == nullptr) || (std::strcmp(p_sub, "list") == 0)) {
        const size_t want = (p_arg != nullptr) ? std::strtoul(p_arg, nullptr, 10) : EVENT_DEFAULT;
        const uint32_t last = diag::events_last_seq();
        after = (last > want) ? static_cast<uint32_t>(last - want) : 0U;
    } else {
        bridge_resp(id, false, "usage: event list [n] | event since <seq>");
        return;
    }
    size_t got = 0U;
    do {
        got = diag::events_since(after, s_event_batch, EVENT_BATCH);
        for (size_t index = 0U; index < got; ++index) {
            const diag::EventRecord& ev = s_event_batch[index];
            line(id, "#%-5u %7u.%03u %c %-8s %s", static_cast<unsigned>(ev.seq),
                 static_cast<unsigned>(ev.ts_ms / MS_PER_S), static_cast<unsigned>(ev.ts_ms % MS_PER_S),
                 SEVERITY[static_cast<size_t>(ev.severity)], ev.p_source, ev.text);
            after = ev.seq;
        }
    } while (got == EVENT_BATCH);
    char text[24];
    (void)std::snprintf(text, sizeof(text), "seq %u", static_cast<unsigned>(diag::events_last_seq()));
    bridge_resp(id, true, text);
}

// ============================================================================
// fault list | fault clear
// ============================================================================
void cmd_fault(uint8_t id, const char* p_sub)
{
    if ((p_sub != nullptr) && (std::strcmp(p_sub, "clear") == 0)) {
        diag::faults_clear();
        bridge_resp(id, true, "fault log cleared");
        return;
    }
    if ((p_sub != nullptr) && (std::strcmp(p_sub, "list") != 0)) {
        bridge_resp(id, false, "usage: fault list|clear");
        return;
    }
    const size_t count = diag::fault_count();
    if (count > 0U) {
        line(id, "%-5s %-9s %-8s %s", "BOOT", "UPTIME", "SOURCE", "FAULT");
    }
    for (size_t index = 0U; index < count; ++index) {
        // "boot|uptime_s|code|text"
        char entry[diag::FAULT_TEXT_LEN];
        (void)std::snprintf(entry, sizeof(entry), "%s", diag::fault_at(index));
        char* p_save = nullptr;
        const char* const p_boot = strtok_r(entry, "|", &p_save);
        const char* const p_up = strtok_r(nullptr, "|", &p_save);
        const char* const p_code = strtok_r(nullptr, "|", &p_save);
        const char* const p_text = strtok_r(nullptr, "", &p_save);
        line(id, "#%-4s %8ss %-8s %s", (p_boot != nullptr) ? p_boot : "?", (p_up != nullptr) ? p_up : "?",
             (p_code != nullptr) ? p_code : "?", (p_text != nullptr) ? p_text : "");
    }
    char text[40];
    (void)std::snprintf(text, sizeof(text), "%u fault(s), kept across reboots", static_cast<unsigned>(count));
    bridge_resp(id, true, text);
}

// ============================================================================
// crash info | crash save | crash clear
// ============================================================================
uint8_t s_crash_chunk[CRASH_CHUNK];

void cmd_crash_info(uint8_t id, const hal::Devices& dev)
{
    hal::CrashSummary crash{};
    const common::Status status = dev.p_system->crash_summary(crash);
    if (status == common::Status::ERR_NOT_FOUND) {
        bridge_resp(id, true, "no crash dump stored");
        return;
    }
    if (unsupported(id, status)) {
        return;
    }
    line(id, "task       %s", crash.task);
    line(id, "reason     %s", (crash.reason[0] != '\0') ? crash.reason : "(unknown)");
    line(id, "pc         0x%08lx", static_cast<unsigned long>(crash.pc));
    line(id, "ra         0x%08lx  (caller)", static_cast<unsigned long>(crash.return_address));
    line(id, "mcause     0x%08lx", static_cast<unsigned long>(crash.cause));
    line(id, "mtval      0x%08lx  (fault address / bad instruction)", static_cast<unsigned long>(crash.fault_address));
    line(id, "dump       %u bytes, firmware %s", static_cast<unsigned>(crash.size), crash.elf_sha);
    line(id, "decode     riscv32-esp-elf-addr2line -pfiaC -e build/<board>/gach_os.elf 0x%08lx 0x%08lx",
         static_cast<unsigned long>(crash.pc), static_cast<unsigned long>(crash.return_address));
    bridge_resp(id, true, "full backtrace: gach crash dump (needs the matching .elf)");
}

void cmd_crash_save(uint8_t id, const hal::Devices& dev)
{
    hal::CrashSummary crash{};
    if (dev.p_system->crash_summary(crash) == common::Status::ERR_NOT_FOUND) {
        bridge_resp(id, false, "no crash dump stored");
        return;
    }
    if ((dev.p_fs == nullptr) || !dev.p_fs->is_mounted()) {
        bridge_resp(id, false, "no SD card");
        return;
    }
    (void)make_dirs(*dev.p_fs, CRASH_DIR);
    hal::FileHandle handle{};
    if (!common::is_ok(dev.p_fs->open(CRASH_FILE, hal::OpenMode::WRITE, handle))) {
        bridge_resp(id, false, "cannot create /crash/coredump.bin");
        return;
    }
    size_t offset = 0U;
    common::Status status = common::Status::OK;
    while (common::is_ok(status)) {
        size_t got = 0U;
        status = dev.p_system->crash_read(offset, s_crash_chunk, sizeof(s_crash_chunk), got);
        if (!common::is_ok(status) || (got == 0U)) {
            break;
        }
        status = dev.p_fs->write(handle, s_crash_chunk, got);
        offset += got;
    }
    dev.p_fs->close(handle);
    if (!common::is_ok(status)) {
        bridge_resp(id, false, "copy failed");
        return;
    }
    char text[64];
    (void)std::snprintf(text, sizeof(text), "/%s (%u bytes)", CRASH_FILE, static_cast<unsigned>(offset));
    bridge_resp(id, true, text);
}

void cmd_crash(uint8_t id, const char* p_sub, const char* p_confirm, const hal::Devices& dev)
{
    if ((p_sub != nullptr) && (std::strcmp(p_sub, "test") == 0)) {
        if ((p_confirm == nullptr) || (std::strcmp(p_confirm, "--yes") != 0)) {
            bridge_resp(id, false, "crash test --yes: deliberately crashes + reboots (checks the core dump path)");
            return;
        }
        bridge_resp(id, true, "crashing now");
        osal::delay_ms(CRASH_TEST_DELAY_MS); // Để khung RESP kịp ra UART.
        __builtin_trap();
    }
    if ((p_sub == nullptr) || (std::strcmp(p_sub, "info") == 0)) {
        cmd_crash_info(id, dev);
    } else if (std::strcmp(p_sub, "save") == 0) {
        cmd_crash_save(id, dev);
    } else if (std::strcmp(p_sub, "clear") == 0) {
        if (!unsupported(id, dev.p_system->crash_erase())) {
            bridge_resp(id, true, "crash dump erased");
        }
    } else {
        bridge_resp(id, false, "usage: crash info|save|clear|test --yes");
    }
}

// ============================================================================
// screenshot [scale]: chụp màn hình ra thẻ SD (tool tải về bằng pull, đổi sang PNG)
// ============================================================================
constexpr uint32_t SHOT_DEFAULT_SCALE = 4U;
constexpr uint32_t SHOT_MAX_SCALE = 8U;
constexpr size_t SHOT_MAX_WIDTH = 2048U;
constexpr const char* SHOT_DIR = "screenshots";
constexpr const char* SHOT_FILE = "screenshots/shot.gshot";

uint16_t s_shot_row[SHOT_MAX_WIDTH];

void cmd_screenshot(uint8_t id, const char* p_scale, const hal::Devices& dev)
{
    const uint32_t scale = (p_scale != nullptr) ? static_cast<uint32_t>(std::strtoul(p_scale, nullptr, 10))
                                                : SHOT_DEFAULT_SCALE;
    if ((scale == 0U) || (scale > SHOT_MAX_SCALE) || (dev.p_display == nullptr)) {
        bridge_resp(id, false, "usage: screenshot [scale 1..8] (default 4)");
        return;
    }
    if ((dev.p_fs == nullptr) || !dev.p_fs->is_mounted()) {
        bridge_resp(id, false, "no SD card");
        return;
    }
    const uint32_t out_w = dev.p_display->width() / scale;
    const uint32_t out_h = dev.p_display->height() / scale;
    if (out_w > SHOT_MAX_WIDTH) {
        bridge_resp(id, false, "screen too wide");
        return;
    }
    (void)make_dirs(*dev.p_fs, SHOT_DIR);
    hal::FileHandle handle{};
    if (!common::is_ok(dev.p_fs->open(SHOT_FILE, hal::OpenMode::WRITE, handle))) {
        bridge_resp(id, false, "cannot create /screenshots/shot.gshot");
        return;
    }
    // Header 8 byte: "GSHT", rộng u16, cao u16 (byte thấp trước); rồi các hàng RGB565.
    const uint8_t header[] = {'G', 'S', 'H', 'T', static_cast<uint8_t>(out_w), static_cast<uint8_t>(out_w >> 8U),
                              static_cast<uint8_t>(out_h), static_cast<uint8_t>(out_h >> 8U)};
    common::Status status = dev.p_fs->write(handle, header, sizeof(header));
    for (uint32_t row = 0U; common::is_ok(status) && (row < out_h); ++row) {
        status = dev.p_display->read_row_scaled(row, scale, s_shot_row);
        if (common::is_ok(status)) {
            status = dev.p_fs->write(handle, reinterpret_cast<const uint8_t*>(s_shot_row), out_w * sizeof(uint16_t));
        }
    }
    dev.p_fs->close(handle);
    if (!common::is_ok(status)) {
        (void)unsupported(id, status);
        return;
    }
    char text[64];
    (void)std::snprintf(text, sizeof(text), "/%s %ux%u", SHOT_FILE, static_cast<unsigned>(out_w),
                        static_cast<unsigned>(out_h));
    bridge_resp(id, true, text);
}

// ============================================================================
// watchdog info
// ============================================================================
void cmd_watchdog(uint8_t id, const hal::Devices& dev)
{
    hal::WatchdogInfo wdt{};
    if (unsupported(id, dev.p_system->get_watchdog_info(wdt))) {
        return;
    }
    if (wdt.task_enabled) {
        line(id, "task WDT       on, timeout %u ms, %s", static_cast<unsigned>(wdt.task_timeout_ms),
             wdt.task_panics ? "panics + reboots" : "only prints a warning");
        for (size_t core = 0U; core < hal::SYSTEM_MAX_CORES; ++core) {
            line(id, "  idle core %u  %s", static_cast<unsigned>(core),
                 wdt.task_idle_core[core] ? "watched (core must not be hogged)" : "not watched");
        }
    } else {
        line(id, "task WDT       off");
    }
    if (wdt.interrupt_enabled) {
        line(id, "interrupt WDT  on, timeout %u ms", static_cast<unsigned>(wdt.interrupt_timeout_ms));
    } else {
        line(id, "interrupt WDT  off");
    }
    bridge_resp(id, true, "a watchdog reset shows in `resetinfo` and `fault list`");
}

} // namespace

bool bridge_diag_command(uint8_t id, size_t argc, char* const p_args[], const hal::Devices& devices)
{
    if ((argc == 0U) || (devices.p_system == nullptr)) {
        return false;
    }
    const char* const p_cmd = p_args[0];
    const char* const p_a1 = (argc > 1U) ? p_args[1] : nullptr;
    const char* const p_a2 = (argc > 2U) ? p_args[2] : nullptr;
    const auto is = [p_cmd](const char* p_name) { return std::strcmp(p_cmd, p_name) == 0; };
    if (is("log")) {
        cmd_log(id, argc, p_args);
    } else if (is("trace")) {
        cmd_trace(id, p_a1);
    } else if (is("event")) {
        cmd_event(id, p_a1, p_a2);
    } else if (is("fault")) {
        cmd_fault(id, p_a1);
    } else if (is("crash")) {
        cmd_crash(id, p_a1, p_a2, devices);
    } else if (is("screenshot")) {
        cmd_screenshot(id, p_a1, devices);
    } else if (is("watchdog")) {
        cmd_watchdog(id, devices);
    } else {
        return false;
    }
    return true;
}

} // namespace runtime
