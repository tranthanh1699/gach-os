/**
 * @file    bridge_sys.cpp
 * @brief   Lệnh introspection của debug bridge: sysinfo, version, uptime, cpuinfo, coreinfo, meminfo, heap
 *          (+ heap trace), memmap, ps, top, taskinfo, stack, resetinfo, bootinfo, stats, health.
 *
 * Chạy trên task bridge (stack nhỏ): mọi mảng lớn là biến tĩnh (runtime .bss nằm trong PSRAM).
 * Số liệu lấy qua hal::ISystem (platform không hỗ trợ -> báo "not supported").
 */
#include "bridge_sys.hpp"

#include <cstdarg>
#include <cstdio>
#include <cstdlib>
#include <cstring>

#include "framework/system.hpp"
#include "framework/ui_lock.hpp"
#include "lvgl.h"
#include "osal/osal.hpp"
#include "runtime/sdk_version.hpp"

namespace runtime {

namespace {

constexpr size_t LINE_LEN = 200U;
constexpr size_t MAX_TASKS = 64U;
constexpr size_t MAX_REGIONS = 8U;
constexpr size_t MAX_PARTITIONS = 16U;
constexpr uint32_t TOP_SAMPLE_MS = 1000U;
constexpr size_t TOP_DEFAULT_ROWS = 15U;
constexpr size_t TRACE_DUMP_DEFAULT = 20U;
constexpr uint32_t PERCENT = 100U;
constexpr uint32_t MS_PER_S = 1000U;
constexpr uint32_t KIB = 1024U;
constexpr uint32_t MIB = KIB * KIB;

// ---- Ngưỡng health ----
constexpr uint32_t INTERNAL_FREE_WARN = 32U * KIB;
constexpr uint32_t INTERNAL_FREE_BAD = 16U * KIB;
constexpr uint32_t INTERNAL_LARGEST_WARN = 8U * KIB;
constexpr uint32_t PSRAM_FREE_WARN = 1U * MIB;
constexpr uint32_t STACK_WARN = 512U;
constexpr uint32_t STACK_BAD = 256U;
constexpr uint8_t CPU_WARN = 90U;
constexpr float TEMP_WARN = 75.0F;
constexpr float TEMP_BAD = 85.0F;
constexpr uint32_t LVGL_USED_WARN = 85U;
constexpr uint32_t SD_FREE_WARN_PCT = 5U;
constexpr int8_t RSSI_WARN = -80;

hal::TaskInfo s_tasks[MAX_TASKS];
hal::TaskInfo s_tasks_prev[MAX_TASKS];
uint16_t s_order[MAX_TASKS];
hal::HeapRegion s_regions[MAX_REGIONS];
hal::FlashPartition s_parts[MAX_PARTITIONS];

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

void fmt_bytes(uint64_t bytes, char* p_out, size_t len)
{
    if (bytes >= MIB) {
        (void)std::snprintf(p_out, len, "%.1f MB", static_cast<double>(bytes) / MIB);
    } else if (bytes >= KIB) {
        (void)std::snprintf(p_out, len, "%.1f KB", static_cast<double>(bytes) / KIB);
    } else {
        (void)std::snprintf(p_out, len, "%u B", static_cast<unsigned>(bytes));
    }
}

void fmt_uptime(uint32_t ms, char* p_out, size_t len)
{
    const uint32_t secs = ms / MS_PER_S;
    const uint32_t days = secs / 86400U;
    (void)std::snprintf(p_out, len, "%ud %02u:%02u:%02u", static_cast<unsigned>(days),
                        static_cast<unsigned>((secs / 3600U) % 24U), static_cast<unsigned>((secs / 60U) % 60U),
                        static_cast<unsigned>(secs % 60U));
}

const char* reset_name(hal::ResetReason reason)
{
    static const char* const NAMES[] = {"unknown",       "power-on",       "external pin", "software restart",
                                        "panic / crash", "interrupt watchdog", "task watchdog", "other watchdog",
                                        "deep sleep wake", "brownout",     "SDIO",         "USB",
                                        "JTAG",          "eFuse error",    "power glitch", "CPU lockup"};
    const auto index = static_cast<size_t>(reason);
    return (index < (sizeof(NAMES) / sizeof(NAMES[0]))) ? NAMES[index] : "unknown";
}

bool is_abnormal(hal::ResetReason reason)
{
    return (reason == hal::ResetReason::PANIC) || (reason == hal::ResetReason::INT_WATCHDOG) ||
           (reason == hal::ResetReason::TASK_WATCHDOG) || (reason == hal::ResetReason::OTHER_WATCHDOG) ||
           (reason == hal::ResetReason::BROWNOUT) || (reason == hal::ResetReason::POWER_GLITCH) ||
           (reason == hal::ResetReason::CPU_LOCKUP);
}

char state_char(hal::TaskState state)
{
    static constexpr char CHARS[] = {'R', 'r', 'B', 'S', 'D', '?'}; // Running, ready, Blocked, Suspended, Deleted
    const auto index = static_cast<size_t>(state);
    return (index < sizeof(CHARS)) ? CHARS[index] : '?';
}

void core_text(int8_t core, char* p_out, size_t len)
{
    if (core < 0) {
        (void)std::snprintf(p_out, len, "any");
    } else {
        (void)std::snprintf(p_out, len, "%d", static_cast<int>(core));
    }
}

bool unsupported(uint8_t id, common::Status status)
{
    if (status == common::Status::ERR_NOT_SUPPORTED) {
        bridge_resp(id, false, "not supported on this platform");
        return true;
    }
    return false;
}

size_t snapshot(const hal::Devices& dev, hal::TaskInfo* p_out, uint64_t& total)
{
    size_t count = 0U;
    if (!common::is_ok(dev.p_system->get_tasks(p_out, MAX_TASKS, count, total))) {
        count = 0U;
    }
    return count;
}

/** @brief Sắp xếp chỉ số task (chèn, n <= 64) theo khóa giảm / tăng dần. */
template <typename Key>
void sort_order(size_t count, Key key, bool descending)
{
    for (size_t index = 0U; index < count; ++index) {
        s_order[index] = static_cast<uint16_t>(index);
    }
    for (size_t index = 1U; index < count; ++index) {
        const uint16_t item = s_order[index];
        size_t pos = index;
        while ((pos > 0U) && (descending ? (key(item) > key(s_order[pos - 1U])) : (key(item) < key(s_order[pos - 1U])))) {
            s_order[pos] = s_order[pos - 1U];
            --pos;
        }
        s_order[pos] = item;
    }
}

// ============================================================================
// Lệnh
// ============================================================================
void cmd_version(uint8_t id, const hal::Devices& dev)
{
    hal::FirmwareInfo fw{};
    if (common::is_ok(dev.p_system->get_firmware_info(fw))) {
        line(id, "firmware   %s %s (built %s %s)", fw.project, fw.version, fw.build_date, fw.build_time);
        line(id, "chip sdk   %s", fw.sdk_version);
    }
    line(id, "gach-os    board %s, app SDK v%u", dev.p_board_name, static_cast<unsigned>(SDK_VERSION));
    line(id, "lvgl       %d.%d.%d", lv_version_major(), lv_version_minor(), lv_version_patch());
    bridge_resp(id, true, nullptr);
}

void cmd_sysinfo(uint8_t id, const hal::Devices& dev)
{
    hal::ChipDetails chip{};
    if (unsupported(id, dev.p_system->get_chip_details(chip))) {
        return;
    }
    char flash[24];
    char psram[24];
    fmt_bytes(chip.flash_bytes, flash, sizeof(flash));
    fmt_bytes(chip.psram_bytes, psram, sizeof(psram));
    line(id, "board      %s", dev.p_board_name);
    line(id, "chip       %s rev v%u.%u, %u core(s) @ %u MHz", chip.model, static_cast<unsigned>(chip.revision / 100U),
         static_cast<unsigned>(chip.revision % 100U), static_cast<unsigned>(chip.cores),
         static_cast<unsigned>(chip.cpu_mhz));
    line(id, "arch       %s", chip.arch);
    line(id, "memory     flash %s, PSRAM %s", flash, psram);
    line(id, "radio      wifi=%s bt=%s 802.15.4=%s (external Wi-Fi: %s)", chip.has_wifi ? "yes" : "no",
         chip.has_bluetooth ? "yes" : "no", chip.has_ieee802154 ? "yes" : "no", (dev.p_wifi != nullptr) ? "yes" : "no");
    if (chip.has_mac) {
        line(id, "mac        %02X:%02X:%02X:%02X:%02X:%02X", chip.mac[0], chip.mac[1], chip.mac[2], chip.mac[3],
             chip.mac[4], chip.mac[5]);
    }
    line(id, "devices    display=%ux%u touch=%s sd=%s debug=%s", static_cast<unsigned>(dev.p_display->width()),
         static_cast<unsigned>(dev.p_display->height()), (dev.p_touch != nullptr) ? "yes" : "no",
         ((dev.p_fs != nullptr) && dev.p_fs->is_mounted()) ? "yes" : "no",
         (dev.p_debug_port != nullptr) ? "uart" : "no");
    bridge_resp(id, true, nullptr);
}

void cmd_uptime(uint8_t id, const hal::Devices& dev)
{
    char up[32];
    fmt_uptime(osal::tick_ms(), up, sizeof(up));
    hal::BootInfo boot{};
    if (common::is_ok(dev.p_system->get_boot_info(boot))) {
        line(id, "up %s, boot #%u, last reset: %s", up, static_cast<unsigned>(boot.boot_count),
             reset_name(boot.reason));
    } else {
        line(id, "up %s", up);
    }
    bridge_resp(id, true, nullptr);
}

void cmd_cpuinfo(uint8_t id, const hal::Devices& dev)
{
    hal::ChipDetails chip{};
    if (unsupported(id, dev.p_system->get_chip_details(chip))) {
        return;
    }
    line(id, "model      %s rev v%u.%u", chip.model, static_cast<unsigned>(chip.revision / 100U),
         static_cast<unsigned>(chip.revision % 100U));
    line(id, "arch       %s", chip.arch);
    line(id, "cores      %u @ %u MHz", static_cast<unsigned>(chip.cores), static_cast<unsigned>(chip.cpu_mhz));
    hal::CpuStats cpu{};
    if (common::is_ok(dev.p_system->get_cpu_stats(cpu))) {
        for (uint8_t core = 0U; core < cpu.core_count; ++core) {
            line(id, "core %u     load %u%%", static_cast<unsigned>(core), static_cast<unsigned>(cpu.load_pct[core]));
        }
        line(id, "tasks      %u", static_cast<unsigned>(cpu.task_count));
    }
    bridge_resp(id, true, nullptr);
}

void cmd_coreinfo(uint8_t id, const hal::Devices& dev)
{
    hal::CpuStats cpu{};
    if (unsupported(id, dev.p_system->get_cpu_stats(cpu))) {
        return;
    }
    uint64_t total = 0U;
    const size_t count = snapshot(dev, s_tasks, total);
    for (uint8_t core = 0U; core < cpu.core_count; ++core) {
        size_t pinned = 0U;
        const char* p_running = "-";
        for (size_t index = 0U; index < count; ++index) {
            pinned += (s_tasks[index].core == static_cast<int8_t>(core)) ? 1U : 0U;
            if ((s_tasks[index].state == hal::TaskState::RUNNING) && (s_tasks[index].core == static_cast<int8_t>(core))) {
                p_running = s_tasks[index].name;
            }
        }
        line(id, "core %u  load %3u%%  pinned tasks %2u  running: %s", static_cast<unsigned>(core),
             static_cast<unsigned>(cpu.load_pct[core]), static_cast<unsigned>(pinned), p_running);
    }
    size_t floating = 0U;
    for (size_t index = 0U; index < count; ++index) {
        floating += (s_tasks[index].core < 0) ? 1U : 0U;
    }
    line(id, "unpinned tasks %u (scheduled on any core)", static_cast<unsigned>(floating));
    bridge_resp(id, true, nullptr);
}

void lvgl_heap(uint8_t id)
{
    lv_mem_monitor_t mon{};
    {
        fw::UiLock lock;
        lv_mem_monitor(&mon);
    }
    char free_text[24];
    char total_text[24];
    fmt_bytes(mon.free_size, free_text, sizeof(free_text));
    fmt_bytes(mon.total_size, total_text, sizeof(total_text));
    line(id, "%-10s %10s free of %-10s used %u%%  frag %u%%  peak %u B", "lvgl", free_text, total_text,
         static_cast<unsigned>(mon.used_pct), static_cast<unsigned>(mon.frag_pct), static_cast<unsigned>(mon.max_used));
}

void cmd_meminfo(uint8_t id, const hal::Devices& dev)
{
    hal::MemoryStats mem{};
    if (unsupported(id, dev.p_system->get_memory_stats(mem))) {
        return;
    }
    char a[24];
    char b[24];
    char c[24];
    char d[24];
    fmt_bytes(mem.internal_free, a, sizeof(a));
    fmt_bytes(mem.internal_total, b, sizeof(b));
    fmt_bytes(mem.internal_min, c, sizeof(c));
    fmt_bytes(mem.internal_largest, d, sizeof(d));
    line(id, "%-10s %10s free of %-10s min %-10s largest block %s", "internal", a, b, c, d);
    if (mem.psram_total > 0U) {
        fmt_bytes(mem.psram_free, a, sizeof(a));
        fmt_bytes(mem.psram_total, b, sizeof(b));
        fmt_bytes(mem.psram_min, c, sizeof(c));
        line(id, "%-10s %10s free of %-10s min %s", "psram", a, b, c);
    }
    lvgl_heap(id);
    bridge_resp(id, true, nullptr);
}

void heap_trace(uint8_t id, const hal::Devices& dev, const char* p_action, const char* p_count)
{
    hal::ISystem& sys = *dev.p_system;
    size_t count = 0U;
    size_t bytes = 0U;
    bool is_running = false;
    if (unsupported(id, sys.heap_trace_status(count, bytes, is_running))) {
        return;
    }
    if ((p_action == nullptr) || (std::strcmp(p_action, "status") == 0)) {
        line(id, "heap trace %s: %u live allocation(s), %u bytes", is_running ? "running" : "stopped",
             static_cast<unsigned>(count), static_cast<unsigned>(bytes));
        bridge_resp(id, true, nullptr);
        return;
    }
    if (std::strcmp(p_action, "start") == 0) {
        const size_t records = (p_count != nullptr) ? static_cast<size_t>(std::strtoul(p_count, nullptr, 10)) : 0U;
        const common::Status status = sys.heap_trace_start(records);
        bridge_resp(id, common::is_ok(status),
                    common::is_ok(status) ? "tracing allocations not yet freed (heap trace stop / dump)"
                                          : ((status == common::Status::ERR_STATE) ? "already running" : "start failed"));
        return;
    }
    if (std::strcmp(p_action, "stop") == 0) {
        const common::Status status = sys.heap_trace_stop();
        bridge_resp(id, common::is_ok(status), common::is_ok(status) ? "stopped" : "not running");
        return;
    }
    if (std::strcmp(p_action, "dump") == 0) {
        const size_t limit = (p_count != nullptr) ? static_cast<size_t>(std::strtoul(p_count, nullptr, 10))
                                                  : TRACE_DUMP_DEFAULT;
        line(id, "%u live allocation(s), %u bytes%s", static_cast<unsigned>(count), static_cast<unsigned>(bytes),
             is_running ? " (still tracing)" : "");
        bool has_callers = false;
        for (size_t index = 0U; (index < count) && (index < limit); ++index) {
            hal::HeapTraceRecord record{};
            if (!common::is_ok(sys.heap_trace_get(index, record))) {
                break;
            }
            if (record.callers[0] == 0U) {
                line(id, "0x%08lx %6u B core%u", static_cast<unsigned long>(record.address),
                     static_cast<unsigned>(record.size), static_cast<unsigned>(record.core));
                continue;
            }
            has_callers = true;
            line(id, "0x%08lx %6u B core%u  from 0x%08lx 0x%08lx 0x%08lx 0x%08lx",
                 static_cast<unsigned long>(record.address), static_cast<unsigned>(record.size),
                 static_cast<unsigned>(record.core), static_cast<unsigned long>(record.callers[0]),
                 static_cast<unsigned long>(record.callers[1]), static_cast<unsigned long>(record.callers[2]),
                 static_cast<unsigned long>(record.callers[3]));
        }
        if (has_callers) {
            line(id, "decode callers: riscv32-esp-elf-addr2line -pfiaC -e build/<board>/gach_os.elf <addr>...");
        } else if (count > 0U) {
            line(id, "no caller addresses: firmware built without frame pointer (see sdkconfig.defaults)");
        } else {
            // Không có khối nào.
        }
        bridge_resp(id, true, nullptr);
        return;
    }
    bridge_resp(id, false, "usage: heap trace [start [records]|stop|dump [n]|status]");
}

void cmd_heap(uint8_t id, const hal::Devices& dev, const char* p_sub, const char* p_action, const char* p_count)
{
    if ((p_sub != nullptr) && (std::strcmp(p_sub, "trace") == 0)) {
        heap_trace(id, dev, p_action, p_count);
        return;
    }
    size_t count = 0U;
    if (unsupported(id, dev.p_system->get_heap_regions(s_regions, MAX_REGIONS, count))) {
        return;
    }
    line(id, "%-10s %10s %10s %10s %10s %8s %8s", "region", "total", "free", "min free", "largest", "used blk",
         "free blk");
    for (size_t index = 0U; index < count; ++index) {
        const hal::HeapRegion& r = s_regions[index];
        line(id, "%-10s %10u %10u %10u %10u %8u %8u", r.name, static_cast<unsigned>(r.total),
             static_cast<unsigned>(r.free), static_cast<unsigned>(r.min_free), static_cast<unsigned>(r.largest),
             static_cast<unsigned>(r.allocated_blocks), static_cast<unsigned>(r.free_blocks));
    }
    lvgl_heap(id);
    bridge_resp(id, true, nullptr);
}

void cmd_memmap(uint8_t id, const hal::Devices& dev)
{
    size_t count = 0U;
    char a[24];
    char b[24];
    line(id, "-- heap regions --");
    if (common::is_ok(dev.p_system->get_heap_regions(s_regions, MAX_REGIONS, count))) {
        for (size_t index = 0U; index < count; ++index) {
            fmt_bytes(s_regions[index].total, a, sizeof(a));
            fmt_bytes(s_regions[index].total - s_regions[index].free, b, sizeof(b));
            line(id, "%-10s %10s  used %s", s_regions[index].name, a, b);
        }
    }
    if (dev.app_arena.size > 0U) {
        fmt_bytes(dev.app_arena.size, a, sizeof(a));
        line(id, "%-10s %10s  Lua VM arena @ %p", "app arena", a, dev.app_arena.p_base);
    }
    line(id, "-- flash partitions --");
    if (common::is_ok(dev.p_system->get_partitions(s_parts, MAX_PARTITIONS, count))) {
        for (size_t index = 0U; index < count; ++index) {
            const hal::FlashPartition& p = s_parts[index];
            fmt_bytes(p.size, a, sizeof(a));
            line(id, "%-12s %-14s 0x%06lx  %10s%s", p.label, p.kind, static_cast<unsigned long>(p.offset), a,
                 p.is_running ? "  <- running" : "");
        }
    }
    hal::FirmwareStats fws{};
    if (common::is_ok(dev.p_system->get_firmware_stats(fws)) && (fws.app_partition > 0U)) {
        fmt_bytes(fws.app_used, a, sizeof(a));
        fmt_bytes(fws.app_partition, b, sizeof(b));
        line(id, "firmware image %s of %s (%u%%)", a, b,
             static_cast<unsigned>((static_cast<uint64_t>(fws.app_used) * PERCENT) / fws.app_partition));
    }
    bridge_resp(id, true, nullptr);
}

void print_task(uint8_t id, const hal::TaskInfo& t, const char* p_cpu)
{
    char core[8];
    core_text(t.core, core, sizeof(core));
    line(id, "%4u  %-16s %c  %4u  %4s  %8u  %s", static_cast<unsigned>(t.id), t.name, state_char(t.state),
         static_cast<unsigned>(t.priority), core, static_cast<unsigned>(t.stack_free_min), p_cpu);
}

void cmd_ps(uint8_t id, const hal::Devices& dev)
{
    uint64_t total = 0U;
    size_t count = 0U;
    if (unsupported(id, dev.p_system->get_tasks(s_tasks, MAX_TASKS, count, total))) {
        return;
    }
    sort_order(count, [](uint16_t i) { return s_tasks[i].id; }, false);
    line(id, "%4s  %-16s %c  %4s  %4s  %8s  %s", "ID", "NAME", 'S', "PRIO", "CORE", "STK FREE", "CPU% (since boot)");
    for (size_t index = 0U; index < count; ++index) {
        const hal::TaskInfo& t = s_tasks[s_order[index]];
        char cpu[16];
        (void)std::snprintf(cpu, sizeof(cpu), "%5.1f",
                            (total > 0U) ? (static_cast<double>(t.runtime) * PERCENT / static_cast<double>(total))
                                         : 0.0);
        print_task(id, t, cpu);
    }
    line(id, "%u task(s); S: R running, r ready, B blocked, S suspended", static_cast<unsigned>(count));
    bridge_resp(id, true, nullptr);
}

void cmd_top(uint8_t id, const hal::Devices& dev, const char* p_rows)
{
    uint64_t total_a = 0U;
    const size_t count_a = snapshot(dev, s_tasks_prev, total_a);
    if (count_a == 0U) {
        bridge_resp(id, false, "not supported on this platform");
        return;
    }
    osal::delay_ms(TOP_SAMPLE_MS);
    uint64_t total_b = 0U;
    const size_t count = snapshot(dev, s_tasks, total_b);
    const uint64_t window = (total_b > total_a) ? (total_b - total_a) : 1U;
    // Thời gian CPU của mỗi task trong cửa sổ, lưu tạm vào runtime (đã chụp xong, không cần giá trị cũ).
    for (size_t index = 0U; index < count; ++index) {
        uint64_t before = s_tasks[index].runtime; // Task mới sinh: tính từ 0.
        for (size_t prev = 0U; prev < count_a; ++prev) {
            if (s_tasks_prev[prev].id == s_tasks[index].id) {
                before = s_tasks_prev[prev].runtime;
                break;
            }
        }
        s_tasks[index].runtime = (s_tasks[index].runtime >= before) ? (s_tasks[index].runtime - before) : 0U;
    }
    sort_order(count, [](uint16_t i) { return s_tasks[i].runtime; }, true);
    hal::CpuStats cpu{};
    if (common::is_ok(dev.p_system->get_cpu_stats(cpu))) {
        char loads[64] = {};
        size_t used = 0U;
        for (uint8_t core = 0U; core < cpu.core_count; ++core) {
            used += static_cast<size_t>(std::snprintf(&loads[used], sizeof(loads) - used, " core%u %u%%",
                                                      static_cast<unsigned>(core),
                                                      static_cast<unsigned>(cpu.load_pct[core])));
        }
        hal::MemoryStats mem{};
        (void)dev.p_system->get_memory_stats(mem);
        line(id, "cpu:%s | tasks %u | internal free %u KB | psram free %u KB", loads, static_cast<unsigned>(count),
             static_cast<unsigned>(mem.internal_free / KIB), static_cast<unsigned>(mem.psram_free / KIB));
    }
    line(id, "%4s  %-16s %c  %4s  %4s  %8s  %s", "ID", "NAME", 'S', "PRIO", "CORE", "STK FREE", "CPU% (1 s, of one core)");
    const size_t rows = (p_rows != nullptr) ? static_cast<size_t>(std::strtoul(p_rows, nullptr, 10)) : TOP_DEFAULT_ROWS;
    for (size_t index = 0U; (index < count) && (index < rows); ++index) {
        const hal::TaskInfo& t = s_tasks[s_order[index]];
        char pct[16];
        (void)std::snprintf(pct, sizeof(pct), "%5.1f", static_cast<double>(t.runtime) * PERCENT / static_cast<double>(window));
        print_task(id, t, pct);
    }
    bridge_resp(id, true, nullptr);
}

void cmd_taskinfo(uint8_t id, const hal::Devices& dev, const char* p_name)
{
    if (p_name == nullptr) {
        bridge_resp(id, false, "usage: taskinfo <name|id>");
        return;
    }
    uint64_t total = 0U;
    const size_t count = snapshot(dev, s_tasks, total);
    const auto wanted_id = static_cast<uint32_t>(std::strtoul(p_name, nullptr, 10));
    for (size_t index = 0U; index < count; ++index) {
        const hal::TaskInfo& t = s_tasks[index];
        if ((std::strcmp(t.name, p_name) != 0) && ((wanted_id == 0U) || (t.id != wanted_id))) {
            continue;
        }
        static const char* const STATES[] = {"running", "ready", "blocked", "suspended", "deleted", "unknown"};
        char core[8];
        core_text(t.core, core, sizeof(core));
        line(id, "name       %s", t.name);
        line(id, "id         %u", static_cast<unsigned>(t.id));
        line(id, "state      %s", STATES[static_cast<size_t>(t.state)]);
        line(id, "priority   %u", static_cast<unsigned>(t.priority));
        line(id, "core       %s", core);
        line(id, "stack      %u bytes free at the lowest point%s", static_cast<unsigned>(t.stack_free_min),
             (t.stack_free_min < STACK_WARN) ? "  (LOW)" : "");
        line(id, "cpu time   %.1f%% of one core since boot",
             (total > 0U) ? (static_cast<double>(t.runtime) * PERCENT / static_cast<double>(total)) : 0.0);
        bridge_resp(id, true, nullptr);
        return;
    }
    bridge_resp(id, false, "no such task (see ps)");
}

void cmd_stack(uint8_t id, const hal::Devices& dev)
{
    uint64_t total = 0U;
    size_t count = 0U;
    if (unsupported(id, dev.p_system->get_tasks(s_tasks, MAX_TASKS, count, total))) {
        return;
    }
    sort_order(count, [](uint16_t i) { return s_tasks[i].stack_free_min; }, false);
    line(id, "%-16s %10s  %s", "TASK", "MIN FREE", "(lowest first; stack size itself is not reported by the RTOS)");
    for (size_t index = 0U; index < count; ++index) {
        const hal::TaskInfo& t = s_tasks[s_order[index]];
        const char* const p_flag = (t.stack_free_min < STACK_BAD) ? "  BAD" : ((t.stack_free_min < STACK_WARN) ? "  LOW" : "");
        line(id, "%-16s %8u B%s", t.name, static_cast<unsigned>(t.stack_free_min), p_flag);
    }
    bridge_resp(id, true, nullptr);
}

void cmd_resetinfo(uint8_t id, const hal::Devices& dev)
{
    hal::BootInfo boot{};
    if (unsupported(id, dev.p_system->get_boot_info(boot))) {
        return;
    }
    line(id, "last reset: %s%s", reset_name(boot.reason), is_abnormal(boot.reason) ? "  (ABNORMAL)" : "");
    if (boot.reason == hal::ResetReason::PANIC) {
        line(id, "hint: run `gach dmesg` right after a crash is not possible (RAM is cleared); use");
        line(id, "      `./build.sh -p <port> monitor` to catch the panic backtrace next time");
    } else if (boot.reason == hal::ResetReason::BROWNOUT) {
        line(id, "hint: supply voltage dropped - use a powered USB port / stronger supply");
    } else if ((boot.reason == hal::ResetReason::TASK_WATCHDOG) || (boot.reason == hal::ResetReason::INT_WATCHDOG)) {
        line(id, "hint: a task or ISR blocked the CPU too long");
    } else {
        // Lý do bình thường.
    }
    bridge_resp(id, true, nullptr);
}

void cmd_bootinfo(uint8_t id, const hal::Devices& dev)
{
    hal::BootInfo boot{};
    if (unsupported(id, dev.p_system->get_boot_info(boot))) {
        return;
    }
    char up[32];
    fmt_uptime(osal::tick_ms(), up, sizeof(up));
    line(id, "boot count %u (since power-on)", static_cast<unsigned>(boot.boot_count));
    line(id, "reset      %s", reset_name(boot.reason));
    line(id, "uptime     %s", up);
    size_t count = 0U;
    if (common::is_ok(dev.p_system->get_partitions(s_parts, MAX_PARTITIONS, count))) {
        for (size_t index = 0U; index < count; ++index) {
            if (s_parts[index].is_running) {
                line(id, "running    %s (%s) @ 0x%06lx", s_parts[index].label, s_parts[index].kind,
                     static_cast<unsigned long>(s_parts[index].offset));
            }
        }
    }
    hal::FirmwareInfo fw{};
    if (common::is_ok(dev.p_system->get_firmware_info(fw))) {
        line(id, "firmware   %s %s", fw.project, fw.version);
    }
    bridge_resp(id, true, nullptr);
}

void cmd_stats(uint8_t id, const hal::Devices& dev)
{
    char up[32];
    fmt_uptime(osal::tick_ms(), up, sizeof(up));
    line(id, "uptime   %s", up);
    hal::CpuStats cpu{};
    if (common::is_ok(dev.p_system->get_cpu_stats(cpu))) {
        line(id, "cpu      core0 %u%%  core1 %u%%  tasks %u", static_cast<unsigned>(cpu.load_pct[0]),
             static_cast<unsigned>(cpu.load_pct[1]), static_cast<unsigned>(cpu.task_count));
    }
    hal::MemoryStats mem{};
    if (common::is_ok(dev.p_system->get_memory_stats(mem))) {
        line(id, "memory   internal %u/%u KB free (min %u)  psram %u/%u KB free",
             static_cast<unsigned>(mem.internal_free / KIB), static_cast<unsigned>(mem.internal_total / KIB),
             static_cast<unsigned>(mem.internal_min / KIB), static_cast<unsigned>(mem.psram_free / KIB),
             static_cast<unsigned>(mem.psram_total / KIB));
    }
    lvgl_heap(id);
    fw::PerfStats perf{};
    {
        fw::UiLock lock;
        fw::perf_snapshot(perf, false);
    }
    if (perf.window_ms > 0U) {
        line(id, "display  %u redraws in %u s (avg %u us/frame)", static_cast<unsigned>(perf.frames),
             static_cast<unsigned>(perf.window_ms / MS_PER_S),
             static_cast<unsigned>((perf.frames > 0U) ? (perf.render_us / perf.frames) : 0U));
    }
    float temp = 0.0F;
    if (common::is_ok(dev.p_system->read_temperature(temp))) {
        line(id, "temp     %.1f C", static_cast<double>(temp));
    }
    uint64_t total = 0U;
    uint64_t free_bytes = 0U;
    if ((dev.p_fs != nullptr) && common::is_ok(dev.p_fs->get_capacity(total, free_bytes)) && (total > 0U)) {
        line(id, "sd       %u MB free of %u MB", static_cast<unsigned>(free_bytes / MIB),
             static_cast<unsigned>(total / MIB));
    }
    if (dev.p_wifi != nullptr) {
        char ip[20] = "-";
        int8_t rssi = 0;
        const bool has_ip = dev.p_wifi->ip_address(ip, sizeof(ip));
        if (dev.p_wifi->link_rssi(rssi)) {
            line(id, "wifi     connected, ip %s, rssi %d dBm", ip, static_cast<int>(rssi));
        } else {
            line(id, "wifi     %s", has_ip ? ip : "not connected");
        }
    }
    bridge_resp(id, true, nullptr);
}

// ---- health ----
enum class Grade : uint8_t { OK, INFO, WARN, BAD };
Grade s_worst = Grade::OK;

void grade(uint8_t id, Grade g, const char* p_fmt, ...) __attribute__((format(printf, 3, 4)));
void grade(uint8_t id, Grade g, const char* p_fmt, ...)
{
    static const char* const TAGS[] = {"[ OK ]", "[INFO]", "[WARN]", "[BAD ]"};
    char text[LINE_LEN];
    va_list args;
    va_start(args, p_fmt);
    (void)std::vsnprintf(text, sizeof(text), p_fmt, args);
    va_end(args);
    line(id, "%s %s", TAGS[static_cast<size_t>(g)], text);
    if (static_cast<uint8_t>(g) > static_cast<uint8_t>(s_worst)) {
        s_worst = g;
    }
}

void cmd_health(uint8_t id, const hal::Devices& dev)
{
    s_worst = Grade::OK;
    hal::MemoryStats mem{};
    if (common::is_ok(dev.p_system->get_memory_stats(mem))) {
        const Grade g = (mem.internal_free < INTERNAL_FREE_BAD)    ? Grade::BAD
                        : (mem.internal_free < INTERNAL_FREE_WARN) ? Grade::WARN
                                                                   : Grade::OK;
        grade(id, g, "internal RAM %u KB free (lowest %u KB)", static_cast<unsigned>(mem.internal_free / KIB),
              static_cast<unsigned>(mem.internal_min / KIB));
        grade(id, (mem.internal_largest < INTERNAL_LARGEST_WARN) ? Grade::WARN : Grade::OK,
              "internal largest block %u KB (fragmentation)", static_cast<unsigned>(mem.internal_largest / KIB));
        if (mem.psram_total > 0U) {
            grade(id, (mem.psram_free < PSRAM_FREE_WARN) ? Grade::WARN : Grade::OK, "PSRAM %u KB free",
                  static_cast<unsigned>(mem.psram_free / KIB));
        }
    }
    lv_mem_monitor_t mon{};
    {
        fw::UiLock lock;
        lv_mem_monitor(&mon);
    }
    grade(id, (mon.used_pct > LVGL_USED_WARN) ? Grade::WARN : Grade::OK, "UI heap %u%% used, %u%% fragmented",
          static_cast<unsigned>(mon.used_pct), static_cast<unsigned>(mon.frag_pct));

    uint64_t total = 0U;
    size_t count = 0U;
    if (common::is_ok(dev.p_system->get_tasks(s_tasks, MAX_TASKS, count, total))) {
        size_t low = 0U;
        for (size_t index = 0U; index < count; ++index) {
            const hal::TaskInfo& t = s_tasks[index];
            if (t.stack_free_min < STACK_WARN) {
                ++low;
                grade(id, (t.stack_free_min < STACK_BAD) ? Grade::BAD : Grade::WARN, "task %s stack only %u B free",
                      t.name, static_cast<unsigned>(t.stack_free_min));
            }
        }
        if (low == 0U) {
            grade(id, Grade::OK, "all %u task stacks have >= %u B headroom", static_cast<unsigned>(count),
                  static_cast<unsigned>(STACK_WARN));
        }
    }
    hal::CpuStats cpu{};
    if (common::is_ok(dev.p_system->get_cpu_stats(cpu))) {
        for (uint8_t core = 0U; core < cpu.core_count; ++core) {
            grade(id, (cpu.load_pct[core] > CPU_WARN) ? Grade::WARN : Grade::OK, "core %u load %u%%",
                  static_cast<unsigned>(core), static_cast<unsigned>(cpu.load_pct[core]));
        }
    }
    float temp = 0.0F;
    if (common::is_ok(dev.p_system->read_temperature(temp))) {
        const Grade g = (temp > TEMP_BAD) ? Grade::BAD : ((temp > TEMP_WARN) ? Grade::WARN : Grade::OK);
        grade(id, g, "chip temperature %.1f C", static_cast<double>(temp));
    }
    uint64_t sd_total = 0U;
    uint64_t sd_free = 0U;
    if ((dev.p_fs == nullptr) || !dev.p_fs->is_mounted()) {
        grade(id, Grade::INFO, "no SD card (installed apps unavailable)");
    } else if (common::is_ok(dev.p_fs->get_capacity(sd_total, sd_free)) && (sd_total > 0U)) {
        const auto pct = static_cast<unsigned>((sd_free * PERCENT) / sd_total);
        grade(id, (pct < SD_FREE_WARN_PCT) ? Grade::WARN : Grade::OK, "SD card %u%% free", pct);
    } else {
        grade(id, Grade::WARN, "SD card mounted but capacity unreadable");
    }
    if (dev.p_wifi != nullptr) {
        int8_t rssi = 0;
        if (!dev.p_wifi->link_rssi(rssi)) {
            grade(id, Grade::INFO, "Wi-Fi not connected");
        } else {
            grade(id, (rssi < RSSI_WARN) ? Grade::WARN : Grade::OK, "Wi-Fi signal %d dBm", static_cast<int>(rssi));
        }
    }
    hal::BootInfo boot{};
    if (common::is_ok(dev.p_system->get_boot_info(boot))) {
        grade(id, is_abnormal(boot.reason) ? Grade::WARN : Grade::OK, "last reset: %s (boot #%u)",
              reset_name(boot.reason), static_cast<unsigned>(boot.boot_count));
    }
    static const char* const OVERALL[] = {"HEALTHY", "HEALTHY", "NEEDS ATTENTION", "UNHEALTHY"};
    bridge_resp(id, s_worst != Grade::BAD, OVERALL[static_cast<size_t>(s_worst)]);
}

} // namespace

bool bridge_sys_command(uint8_t id, size_t argc, char* const p_args[], const hal::Devices& devices)
{
    if ((argc == 0U) || (devices.p_system == nullptr)) {
        return false;
    }
    const char* const p_cmd = p_args[0];
    const char* const p_a1 = (argc > 1U) ? p_args[1] : nullptr;
    const char* const p_a2 = (argc > 2U) ? p_args[2] : nullptr;
    const char* const p_a3 = (argc > 3U) ? p_args[3] : nullptr;
    const auto is = [p_cmd](const char* p_name) { return std::strcmp(p_cmd, p_name) == 0; };
    if (is("sysinfo")) {
        cmd_sysinfo(id, devices);
    } else if (is("version")) {
        cmd_version(id, devices);
    } else if (is("uptime")) {
        cmd_uptime(id, devices);
    } else if (is("cpuinfo")) {
        cmd_cpuinfo(id, devices);
    } else if (is("coreinfo")) {
        cmd_coreinfo(id, devices);
    } else if (is("meminfo")) {
        cmd_meminfo(id, devices);
    } else if (is("heap")) {
        cmd_heap(id, devices, p_a1, p_a2, p_a3);
    } else if (is("memmap")) {
        cmd_memmap(id, devices);
    } else if (is("ps")) {
        cmd_ps(id, devices);
    } else if (is("top")) {
        cmd_top(id, devices, p_a1);
    } else if (is("taskinfo")) {
        cmd_taskinfo(id, devices, p_a1);
    } else if (is("stack")) {
        cmd_stack(id, devices);
    } else if (is("resetinfo")) {
        cmd_resetinfo(id, devices);
    } else if (is("bootinfo")) {
        cmd_bootinfo(id, devices);
    } else if (is("stats")) {
        cmd_stats(id, devices);
    } else if (is("health")) {
        cmd_health(id, devices);
    } else {
        return false;
    }
    return true;
}

} // namespace runtime
