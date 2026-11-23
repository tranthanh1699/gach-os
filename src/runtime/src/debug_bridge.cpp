/**
 * @file    debug_bridge.cpp
 * @brief   Debug bridge kiểu adb trên hal::IDebugPort: lệnh shell, đẩy/kéo file, cài/gỡ app, log.
 *
 * Giao thức (chi tiết: docs/debug_bridge.md). Khung = 0x00 | COBS(type | id | data | crc16_le) | 0x00.
 *   Máy tính -> board : CMD (dòng lệnh), DATA (đoạn file khi push), ACK (xác nhận đoạn khi pull), PING
 *   Board -> máy tính : RESP (status | chữ), TEXT (dòng kết quả dài), DATA, ACK, LOG, HELLO
 * Khi chưa có máy tính kết nối, log đi ra dạng chữ thô như bình thường (idf.py monitor vẫn đọc được);
 * có khung hợp lệ từ máy tính -> chuyển sang đóng khung log. Im lặng quá SESSION_TIMEOUT_MS -> về chữ thô.
 */
#include <cstdio>
#include <cstdlib>
#include <cstring>

#include "frame_codec.hpp"
#include "framework/system.hpp"
#include "framework/ui_lock.hpp"
#include "hal_api/board.hpp"
#include "lvgl.h"
#include "osal/osal.hpp"
#include "runtime/runtime.hpp"
#include "runtime/sdk_version.hpp"

namespace runtime {

namespace {

// ---- Giao thức ----
constexpr uint8_t PROTOCOL_VERSION = 1U;
constexpr uint8_t TYPE_CMD = 0x01U;
constexpr uint8_t TYPE_DATA_IN = 0x02U;
constexpr uint8_t TYPE_ACK_IN = 0x03U;
constexpr uint8_t TYPE_PING = 0x04U;
constexpr uint8_t TYPE_RESP = 0x81U;
constexpr uint8_t TYPE_DATA_OUT = 0x82U;
constexpr uint8_t TYPE_ACK_OUT = 0x83U;
constexpr uint8_t TYPE_LOG = 0x84U;
constexpr uint8_t TYPE_HELLO = 0x85U;
constexpr uint8_t TYPE_TEXT = 0x86U;
constexpr uint8_t STATUS_OK = 0U;
constexpr uint8_t STATUS_ERROR = 1U;
constexpr uint8_t FRAME_DELIMITER = 0x00U;

constexpr size_t HEADER_BYTES = 2U;                   /**< type + id */
constexpr size_t CRC_BYTES = 2U;
constexpr size_t MAX_DATA = 256U;                     /**< Dữ liệu tối đa mỗi khung. */
constexpr size_t MAX_PAYLOAD = HEADER_BYTES + MAX_DATA + CRC_BYTES;
constexpr size_t MAX_ENCODED = MAX_PAYLOAD + (MAX_PAYLOAD / 254U) + 2U;
constexpr size_t RX_LINE_MAX = MAX_ENCODED + 8U;      /**< Khung dài hơn -> rác, bỏ qua. */
constexpr size_t TEXT_BUF_LEN = 200U;
constexpr size_t CMD_ARGS_MAX = 4U;
constexpr uint8_t BYTE_MASK = 0xFFU;
constexpr uint8_t BYTE_SHIFT = 8U;

// ---- Thời gian ----
constexpr uint32_t READ_TIMEOUT_MS = 50U;
constexpr uint32_t SESSION_TIMEOUT_MS = 5000U;        /**< Tool gửi PING mỗi ~1 s khi đang kết nối. */
constexpr uint32_t PULL_ACK_TIMEOUT_MS = 2000U;
constexpr uint32_t LOG_LOCK_TIMEOUT_MS = 20U;         /**< Không chờ lâu trong đường log (có thể gọi lồng). */
constexpr uint32_t REBOOT_DELAY_MS = 200U;            /**< Cho RESP kịp ra dây trước khi khởi động lại. */
constexpr uint32_t TASK_STACK_BYTES = 6U * 1024U;
constexpr uint8_t TASK_PRIORITY = 2U;                 /**< Dưới task UI: bridge không làm giật giao diện. */
constexpr int8_t TASK_CORE = 0;                        /**< Việc nền ở core 0, UI ở core 1 (fw::config::UI_TASK_CORE). */
constexpr size_t READ_CHUNK = 128U;

const hal::Devices* s_p_devices = nullptr;
hal::IDebugPort* s_p_port = nullptr;
bool s_is_tx_busy = false;                            /**< Chống vào lại cùng task (mutex OSAL là đệ quy). */
bool s_is_running = false;
volatile bool s_is_session = false;                   /**< Có máy tính đang kết nối (log đóng khung). */
volatile uint32_t s_last_rx_ms = 0U;

uint8_t s_tx_payload[MAX_PAYLOAD];
uint8_t s_tx_frame[MAX_ENCODED + 2U];
uint8_t s_rx_line[RX_LINE_MAX];
size_t s_rx_len = 0U;
bool s_rx_overflow = false;
uint8_t s_rx_payload[MAX_PAYLOAD];

// Trạng thái push (máy tính -> board).
hal::FileHandle s_push_handle = hal::INVALID_FILE;
uint32_t s_push_remaining = 0U;
uint8_t s_push_id = 0U;

/** @brief Một khung đã giải mã. */
struct Frame {
    uint8_t type;
    uint8_t id;
    const uint8_t* p_data;
    size_t len;
};

osal::Mutex& tx_mutex()
{
    static osal::Mutex s_mutex; // Tạo khi dùng lần đầu (sau khi RTOS sẵn sàng).
    return s_mutex;
}

/** @brief Đóng khung và ghi ra cổng (một lần ghi liền khối). */
bool send_frame(uint8_t type, uint8_t id, const uint8_t* p_data, size_t len, uint32_t lock_timeout_ms)
{
    if ((s_p_port == nullptr) || (len > MAX_DATA) || !tx_mutex().lock(lock_timeout_ms)) {
        return false;
    }
    if (s_is_tx_busy) { // Log phát sinh ngay trong đường ghi (driver UART) -> bỏ, không ghi đè buffer đang gửi.
        tx_mutex().unlock();
        return false;
    }
    s_is_tx_busy = true;
    s_tx_payload[0] = type;
    s_tx_payload[1] = id;
    if (len > 0U) {
        (void)std::memcpy(&s_tx_payload[HEADER_BYTES], p_data, len);
    }
    const uint16_t crc = codec::crc16(s_tx_payload, HEADER_BYTES + len);
    s_tx_payload[HEADER_BYTES + len] = static_cast<uint8_t>(crc & BYTE_MASK);
    s_tx_payload[HEADER_BYTES + len + 1U] = static_cast<uint8_t>(crc >> BYTE_SHIFT);
    s_tx_frame[0] = FRAME_DELIMITER;
    const size_t encoded = codec::cobs_encode(s_tx_payload, HEADER_BYTES + len + CRC_BYTES, &s_tx_frame[1],
                                              sizeof(s_tx_frame) - 2U);
    bool is_ok = false;
    if (encoded > 0U) {
        s_tx_frame[encoded + 1U] = FRAME_DELIMITER;
        is_ok = common::is_ok(s_p_port->write(s_tx_frame, encoded + 2U));
    }
    s_is_tx_busy = false;
    tx_mutex().unlock();
    return is_ok;
}

void send_text(uint8_t id, const char* p_text)
{
    (void)send_frame(TYPE_TEXT, id, reinterpret_cast<const uint8_t*>(p_text), std::strlen(p_text), osal::WAIT_FOREVER);
}

/** @brief Phản hồi kết thúc một lệnh: [status][chữ]. */
void send_resp(uint8_t id, bool is_ok, const char* p_text)
{
    uint8_t data[TEXT_BUF_LEN + 1U];
    data[0] = is_ok ? STATUS_OK : STATUS_ERROR;
    size_t len = (p_text != nullptr) ? std::strlen(p_text) : 0U;
    len = (len > TEXT_BUF_LEN) ? TEXT_BUF_LEN : len;
    if (len > 0U) {
        (void)std::memcpy(&data[1], p_text, len);
    }
    (void)send_frame(TYPE_RESP, id, data, len + 1U, osal::WAIT_FOREVER);
}

hal::IFileSystem* filesystem()
{
    hal::IFileSystem* const p_fs = (s_p_devices != nullptr) ? s_p_devices->p_fs : nullptr;
    return ((p_fs != nullptr) && p_fs->is_mounted()) ? p_fs : nullptr;
}

/** @brief Đường dẫn trên thẻ: bỏ '/' ở đầu (HAL dùng đường dẫn tương đối gốc thẻ). */
const char* fs_path(const char* p_path)
{
    while ((p_path != nullptr) && (*p_path == '/')) {
        ++p_path;
    }
    return p_path;
}

// ============================================================================
// Lệnh
// ============================================================================
void cmd_help(uint8_t id)
{
    send_text(id, "info | mem | apps | ls [dir] | mkdir <dir> | rm <path>");
    send_text(id, "push <path> <size> | pull <path> | install | uninstall <id> | reboot");
    send_text(id, "launch <app_id> | back | home | perf [reset|bench] | uidiag");
    send_resp(id, true, nullptr);
}

void cmd_info(uint8_t id)
{
    hal::SystemInfo info{};
    s_p_devices->p_system->get_info(info);
    char text[TEXT_BUF_LEN];
    (void)std::snprintf(text, sizeof(text), "board=%s chip=%s cores=%u mhz=%u flash_kb=%u sdk=%u bridge=%u sd=%s",
                        s_p_devices->p_board_name, (info.p_chip_name != nullptr) ? info.p_chip_name : "?",
                        static_cast<unsigned>(info.cpu_cores), static_cast<unsigned>(info.cpu_freq_mhz),
                        static_cast<unsigned>(info.flash_size_kb), static_cast<unsigned>(SDK_VERSION),
                        static_cast<unsigned>(PROTOCOL_VERSION), (filesystem() != nullptr) ? "yes" : "no");
    send_resp(id, true, text);
}

void cmd_mem(uint8_t id)
{
    hal::SystemInfo info{};
    s_p_devices->p_system->get_info(info);
    char text[TEXT_BUF_LEN];
    (void)std::snprintf(text, sizeof(text), "heap_free=%u heap_min=%u uptime_ms=%u",
                        static_cast<unsigned>(info.heap_free_bytes), static_cast<unsigned>(info.heap_min_bytes),
                        static_cast<unsigned>(osal::tick_ms()));
    hal::MemoryStats mem{};
    if (s_p_devices->p_system->get_memory_stats(mem) == common::Status::OK) {
        char detail[TEXT_BUF_LEN];
        (void)std::snprintf(detail, sizeof(detail),
                            "internal free=%u min=%u largest=%u total=%u | psram free=%u min=%u total=%u",
                            static_cast<unsigned>(mem.internal_free), static_cast<unsigned>(mem.internal_min),
                            static_cast<unsigned>(mem.internal_largest), static_cast<unsigned>(mem.internal_total),
                            static_cast<unsigned>(mem.psram_free), static_cast<unsigned>(mem.psram_min),
                            static_cast<unsigned>(mem.psram_total));
        send_text(id, detail);
    }
    lv_mem_monitor_t mon{};
    {
        fw::UiLock lock; // Heap LVGL chỉ an toàn khi đọc trong ngữ cảnh UI.
        lv_mem_monitor(&mon);
    }
    char lvgl_text[TEXT_BUF_LEN];
    (void)std::snprintf(lvgl_text, sizeof(lvgl_text), "lvgl free=%u total=%u used=%u%% frag=%u%% max_used=%u",
                        static_cast<unsigned>(mon.free_size), static_cast<unsigned>(mon.total_size),
                        static_cast<unsigned>(mon.used_pct), static_cast<unsigned>(mon.frag_pct),
                        static_cast<unsigned>(mon.max_used));
    send_text(id, lvgl_text);
    send_resp(id, true, text);
}

void cmd_apps(uint8_t id)
{
    size_t count = 0U;
    {
        fw::UiLock lock;
        count = package_count();
        for (size_t index = 0U; index < count; ++index) {
            PackageView view{};
            if (package_at(index, view)) {
                char text[TEXT_BUF_LEN];
                (void)std::snprintf(text, sizeof(text), "%s\t%s\t%s", view.p_id, view.p_version, view.p_name);
                send_text(id, text);
            }
        }
    }
    char text[TEXT_BUF_LEN];
    (void)std::snprintf(text, sizeof(text), "%u package(s)", static_cast<unsigned>(count));
    send_resp(id, true, text);
}

struct ListContext {
    uint8_t id;
    size_t count;
};

bool list_entry(const hal::DirEntry& entry, void* p_ctx)
{
    auto* const p_list = static_cast<ListContext*>(p_ctx);
    char text[TEXT_BUF_LEN];
    (void)std::snprintf(text, sizeof(text), "%s%s", entry.name, entry.is_dir ? "/" : "");
    send_text(p_list->id, text);
    ++p_list->count;
    return true;
}

void cmd_ls(uint8_t id, const char* p_path)
{
    hal::IFileSystem* const p_fs = filesystem();
    if (p_fs == nullptr) {
        send_resp(id, false, "no sd card");
        return;
    }
    ListContext ctx{id, 0U};
    common::Status status = common::Status::OK;
    {
        fw::UiLock lock;
        status = p_fs->list_dir(fs_path((p_path != nullptr) ? p_path : ""), &list_entry, &ctx);
    }
    send_resp(id, common::is_ok(status), common::is_ok(status) ? nullptr : "cannot list directory");
}

void cmd_simple_fs(uint8_t id, const char* p_cmd, const char* p_path)
{
    hal::IFileSystem* const p_fs = filesystem();
    if ((p_fs == nullptr) || (p_path == nullptr)) {
        send_resp(id, false, (p_fs == nullptr) ? "no sd card" : "missing path");
        return;
    }
    common::Status status = common::Status::OK;
    {
        fw::UiLock lock;
        status = (std::strcmp(p_cmd, "rm") == 0) ? p_fs->remove(fs_path(p_path)) : p_fs->make_dir(fs_path(p_path));
    }
    send_resp(id, common::is_ok(status), common::is_ok(status) ? nullptr : "failed");
}

void close_push()
{
    hal::IFileSystem* const p_fs = filesystem();
    if ((s_push_handle != hal::INVALID_FILE) && (p_fs != nullptr)) {
        fw::UiLock lock;
        p_fs->close(s_push_handle);
    }
    s_push_handle = hal::INVALID_FILE;
    s_push_remaining = 0U;
}

void cmd_push(uint8_t id, const char* p_path, const char* p_size)
{
    hal::IFileSystem* const p_fs = filesystem();
    if ((p_fs == nullptr) || (p_path == nullptr) || (p_size == nullptr)) {
        send_resp(id, false, (p_fs == nullptr) ? "no sd card" : "usage: push <path> <size>");
        return;
    }
    close_push();
    common::Status status = common::Status::OK;
    {
        fw::UiLock lock;
        status = p_fs->open(fs_path(p_path), hal::OpenMode::WRITE, s_push_handle);
    }
    if (!common::is_ok(status)) {
        s_push_handle = hal::INVALID_FILE;
        send_resp(id, false, "cannot open file");
        return;
    }
    s_push_remaining = static_cast<uint32_t>(std::strtoul(p_size, nullptr, 10));
    s_push_id = id;
    if (s_push_remaining == 0U) {
        close_push();
    }
    send_resp(id, true, "ready"); // Máy tính bắt đầu gửi DATA (nếu size > 0).
}

/** @brief Một đoạn dữ liệu push: ghi rồi ACK; đủ kích thước -> đóng file và RESP kết thúc. */
void on_push_data(const Frame& frame)
{
    hal::IFileSystem* const p_fs = filesystem();
    if ((s_push_handle == hal::INVALID_FILE) || (p_fs == nullptr) || (frame.len > s_push_remaining)) {
        const uint8_t status = STATUS_ERROR;
        (void)send_frame(TYPE_ACK_OUT, frame.id, &status, 1U, osal::WAIT_FOREVER);
        close_push();
        return;
    }
    // Không giữ UiLock khi ghi: file đã mở thuộc riêng bridge, FILE* an toàn đa luồng; chỉ open/close
    // đụng bảng slot dùng chung. Giữ khóa ở đây sẽ phải chờ UI vẽ xong mỗi khung (chậm gấp ~3 lần).
    const common::Status write_status = p_fs->write(s_push_handle, frame.p_data, frame.len);
    const uint8_t status = common::is_ok(write_status) ? STATUS_OK : STATUS_ERROR;
    (void)send_frame(TYPE_ACK_OUT, frame.id, &status, 1U, osal::WAIT_FOREVER);
    if (!common::is_ok(write_status)) {
        close_push();
        return;
    }
    s_push_remaining -= static_cast<uint32_t>(frame.len);
    if (s_push_remaining == 0U) {
        close_push();
        send_resp(s_push_id, true, "done");
    }
}

bool read_frame(Frame& frame, uint32_t timeout_ms); // Dùng trong pull (chờ ACK).

void cmd_pull(uint8_t id, const char* p_path)
{
    hal::IFileSystem* const p_fs = filesystem();
    if ((p_fs == nullptr) || (p_path == nullptr)) {
        send_resp(id, false, (p_fs == nullptr) ? "no sd card" : "missing path");
        return;
    }
    hal::FileHandle handle = hal::INVALID_FILE;
    common::Status status = common::Status::OK;
    {
        fw::UiLock lock;
        status = p_fs->open(fs_path(p_path), hal::OpenMode::READ, handle);
    }
    if (!common::is_ok(status)) {
        send_resp(id, false, "cannot open file");
        return;
    }
    send_resp(id, true, "sending");
    uint8_t chunk[MAX_DATA];
    uint8_t seq = 0U;
    bool is_ok = true;
    for (;;) {
        size_t got = 0U;
        status = p_fs->read(handle, chunk, sizeof(chunk), got); // File riêng của bridge: không cần UiLock.
        if (!common::is_ok(status)) {
            is_ok = false;
            break;
        }
        // Đoạn rỗng = hết file. Mỗi đoạn chờ ACK của máy tính (dừng-và-chờ: chắc chắn qua UART).
        (void)send_frame(TYPE_DATA_OUT, seq, chunk, got, osal::WAIT_FOREVER);
        Frame ack{};
        bool is_acked = false;
        const uint32_t start = osal::tick_ms();
        while (!is_acked && ((osal::tick_ms() - start) < PULL_ACK_TIMEOUT_MS)) {
            is_acked = read_frame(ack, READ_TIMEOUT_MS) && (ack.type == TYPE_ACK_IN) && (ack.id == seq);
        }
        if (!is_acked) {
            is_ok = false;
            break;
        }
        if (got == 0U) {
            break;
        }
        ++seq;
    }
    {
        fw::UiLock lock;
        p_fs->close(handle);
    }
    if (!is_ok) {
        send_resp(id, false, "transfer aborted");
    }
}

void cmd_install(uint8_t id)
{
    size_t installed = 0U;
    common::Status status = common::Status::OK;
    {
        fw::UiLock lock; // Đăng ký app vào AppManager phải chạy cùng ngữ cảnh với UI.
        status = install_pending(installed);
    }
    char text[TEXT_BUF_LEN];
    if (common::is_ok(status)) {
        (void)std::snprintf(text, sizeof(text), "installed %u package(s)%s%s", static_cast<unsigned>(installed),
                            (last_error()[0] != '\0') ? " - " : "", last_error());
    } else {
        (void)std::snprintf(text, sizeof(text), "install failed: %s", last_error());
    }
    send_resp(id, common::is_ok(status), text);
}

void cmd_uninstall(uint8_t id, const char* p_id)
{
    if (p_id == nullptr) {
        send_resp(id, false, "usage: uninstall <id>");
        return;
    }
    common::Status status = common::Status::OK;
    {
        fw::UiLock lock;
        status = uninstall(p_id);
    }
    send_resp(id, common::is_ok(status), common::is_ok(status) ? "removed" : last_error());
}

void cmd_launch(uint8_t id, const char* p_app)
{
    if (p_app == nullptr) {
        send_resp(id, false, "usage: launch <app_id>");
        return;
    }
    common::Status status = common::Status::OK;
    {
        fw::UiLock lock;
        status = fw::app_manager().launch(p_app);
    }
    send_resp(id, common::is_ok(status), common::is_ok(status) ? nullptr : "no such app");
}

void cmd_navigate(uint8_t id, bool is_home)
{
    {
        fw::UiLock lock;
        if (is_home) {
            fw::app_manager().home();
        } else {
            fw::app_manager().back();
        }
    }
    send_resp(id, true, nullptr);
}

/** @brief Hiệu năng vẽ của LVGL trong cửa sổ đo hiện tại (perf reset = bắt đầu cửa sổ mới). */
void cmd_perf(uint8_t id, const char* p_arg)
{
    fw::PerfStats stats{};
    const bool is_reset = (p_arg != nullptr) && (std::strcmp(p_arg, "reset") == 0);
    const bool is_bench = (p_arg != nullptr) && (std::strcmp(p_arg, "bench") == 0);
    constexpr uint32_t BENCH_ROUNDS = 20U;
    constexpr uint32_t BENCH_POLL_MS = 50U;
    constexpr uint32_t BENCH_TIMEOUT_MS = 15000U;
    if (is_bench) {
        // Vẽ phải chạy trên stack của task UI (stack bridge nhỏ): đặt lịch rồi chờ ngoài khóa.
        {
            fw::UiLock lock;
            fw::perf_bench_start(BENCH_ROUNDS);
        }
        bool is_done = false;
        for (uint32_t waited = 0U; !is_done && (waited < BENCH_TIMEOUT_MS); waited += BENCH_POLL_MS) {
            osal::delay_ms(BENCH_POLL_MS);
            fw::UiLock lock;
            is_done = fw::perf_bench_result(stats);
        }
        if (!is_done) {
            send_resp(id, false, "bench timeout");
            return;
        }
    } else {
        fw::UiLock lock;
        fw::perf_snapshot(stats, is_reset);
    }
    constexpr uint32_t US_PER_MS = 1000U;
    constexpr uint32_t MS_PER_S = 1000U;
    constexpr uint32_t PERCENT = 100U;
    const uint32_t frames = (stats.frames == 0U) ? 1U : stats.frames;
    const uint32_t window = (stats.window_ms == 0U) ? 1U : stats.window_ms;
    char text[TEXT_BUF_LEN];
    (void)std::snprintf(text, sizeof(text),
                        "window=%ums frames=%u fps=%u.%u avg_frame=%uus max_frame=%uus flush=%u%% of render "
                        "flushes/frame=%u px/frame=%u busy=%u%%",
                        static_cast<unsigned>(stats.window_ms), static_cast<unsigned>(stats.frames),
                        static_cast<unsigned>((stats.frames * MS_PER_S) / window),
                        static_cast<unsigned>(((stats.frames * MS_PER_S * 10U) / window) % 10U),
                        static_cast<unsigned>(stats.render_us / frames), static_cast<unsigned>(stats.max_frame_us),
                        static_cast<unsigned>((stats.render_us == 0U) ? 0U
                                                                      : ((static_cast<uint64_t>(stats.flush_us) * PERCENT) /
                                                                         stats.render_us)),
                        static_cast<unsigned>(stats.flush_count / frames), static_cast<unsigned>(stats.flushed_px / frames),
                        static_cast<unsigned>((static_cast<uint64_t>(stats.render_us) * PERCENT) /
                                              (static_cast<uint64_t>(window) * US_PER_MS)));
    send_resp(id, true, text);
}

/** @brief Tách dòng lệnh thành các từ (sửa tại chỗ). */
size_t split_args(char* p_line, char* p_args[], size_t max_args)
{
    size_t count = 0U;
    char* p = p_line;
    while ((*p != '\0') && (count < max_args)) {
        while (*p == ' ') {
            *p++ = '\0';
        }
        if (*p == '\0') {
            break;
        }
        p_args[count++] = p;
        while ((*p != '\0') && (*p != ' ')) {
            ++p;
        }
    }
    return count;
}

void on_command(const Frame& frame)
{
    char line[MAX_DATA + 1U];
    (void)std::memcpy(line, frame.p_data, frame.len);
    line[frame.len] = '\0';
    char* args[CMD_ARGS_MAX] = {};
    const size_t argc = split_args(line, args, CMD_ARGS_MAX);
    const char* const p_cmd = (argc > 0U) ? args[0] : "";
    const char* const p_arg1 = (argc > 1U) ? args[1] : nullptr;
    const char* const p_arg2 = (argc > 2U) ? args[2] : nullptr;
    const uint8_t id = frame.id;

    if ((std::strcmp(p_cmd, "help") == 0) || (p_cmd[0] == '\0')) {
        cmd_help(id);
    } else if (std::strcmp(p_cmd, "info") == 0) {
        cmd_info(id);
    } else if (std::strcmp(p_cmd, "mem") == 0) {
        cmd_mem(id);
    } else if (std::strcmp(p_cmd, "apps") == 0) {
        cmd_apps(id);
    } else if (std::strcmp(p_cmd, "ls") == 0) {
        cmd_ls(id, p_arg1);
    } else if ((std::strcmp(p_cmd, "rm") == 0) || (std::strcmp(p_cmd, "mkdir") == 0)) {
        cmd_simple_fs(id, p_cmd, p_arg1);
    } else if (std::strcmp(p_cmd, "push") == 0) {
        cmd_push(id, p_arg1, p_arg2);
    } else if (std::strcmp(p_cmd, "pull") == 0) {
        cmd_pull(id, p_arg1);
    } else if (std::strcmp(p_cmd, "install") == 0) {
        cmd_install(id);
    } else if (std::strcmp(p_cmd, "uninstall") == 0) {
        cmd_uninstall(id, p_arg1);
    } else if (std::strcmp(p_cmd, "launch") == 0) {
        cmd_launch(id, p_arg1);
    } else if ((std::strcmp(p_cmd, "home") == 0) || (std::strcmp(p_cmd, "back") == 0)) {
        cmd_navigate(id, std::strcmp(p_cmd, "home") == 0);
    } else if (std::strcmp(p_cmd, "uidiag") == 0) {
        fw::UiDiag diag{};
        fw::ui_diag(diag); // Không khóa: đọc được cả khi UI treo.
        char text[TEXT_BUF_LEN];
        (void)std::snprintf(text, sizeof(text), "loops=%u phase=%u for=%ums", static_cast<unsigned>(diag.loop_count),
                            static_cast<unsigned>(diag.phase),
                            static_cast<unsigned>(osal::tick_ms() - diag.phase_since_ms));
        send_resp(id, true, text);
    } else if (std::strcmp(p_cmd, "perf") == 0) {
        cmd_perf(id, p_arg1);
    } else if (std::strcmp(p_cmd, "reboot") == 0) {
        send_resp(id, true, "rebooting");
        osal::delay_ms(REBOOT_DELAY_MS);
        s_p_devices->p_system->restart();
    } else {
        send_resp(id, false, "unknown command (try: help)");
    }
}

// ============================================================================
// Nhận khung
// ============================================================================
/** @brief Xử lý một khối byte giữa hai byte 0: giải COBS, kiểm CRC. */
bool decode_line(Frame& frame)
{
    const size_t len = codec::cobs_decode(s_rx_line, s_rx_len, s_rx_payload, sizeof(s_rx_payload));
    if (len < (HEADER_BYTES + CRC_BYTES)) {
        return false;
    }
    const size_t body = len - CRC_BYTES;
    const uint16_t crc = static_cast<uint16_t>(s_rx_payload[body] | (s_rx_payload[body + 1U] << BYTE_SHIFT));
    if (crc != codec::crc16(s_rx_payload, body)) {
        return false;
    }
    frame = Frame{s_rx_payload[0], s_rx_payload[1], &s_rx_payload[HEADER_BYTES], body - HEADER_BYTES};
    return true;
}

uint8_t s_rx_chunk[READ_CHUNK];
size_t s_rx_chunk_len = 0U;
size_t s_rx_chunk_pos = 0U;

bool read_frame(Frame& frame, uint32_t timeout_ms)
{
    const uint32_t start = osal::tick_ms();
    do {
        while (s_rx_chunk_pos < s_rx_chunk_len) {
            const uint8_t byte = s_rx_chunk[s_rx_chunk_pos++];
            if (byte != FRAME_DELIMITER) {
                if (s_rx_len < sizeof(s_rx_line)) {
                    s_rx_line[s_rx_len++] = byte;
                } else {
                    s_rx_overflow = true; // Rác quá dài: bỏ tới byte 0 kế tiếp.
                }
                continue;
            }
            const bool is_frame = (s_rx_len > 0U) && !s_rx_overflow && decode_line(frame);
            s_rx_len = 0U;
            s_rx_overflow = false;
            if (is_frame) {
                s_last_rx_ms = osal::tick_ms();
                s_is_session = true;
                return true;
            }
        }
        s_rx_chunk_len = s_p_port->read(s_rx_chunk, sizeof(s_rx_chunk), READ_TIMEOUT_MS);
        s_rx_chunk_pos = 0U;
    } while ((osal::tick_ms() - start) < timeout_ms);
    return false;
}

void bridge_task(void* p_arg)
{
    (void)p_arg;
    char hello[TEXT_BUF_LEN];
    (void)std::snprintf(hello, sizeof(hello), "gach-bridge %u %s", static_cast<unsigned>(PROTOCOL_VERSION),
                        s_p_devices->p_board_name);
    // Không phát gì cho tới khi máy tính lên tiếng: idf.py monitor vẫn chỉ thấy log chữ thô.
    for (;;) {
        Frame frame{};
        if (!read_frame(frame, READ_TIMEOUT_MS)) {
            if (s_is_session && ((osal::tick_ms() - s_last_rx_ms) > SESSION_TIMEOUT_MS)) {
                s_is_session = false; // Máy tính đã ngắt: log về dạng chữ thô.
                close_push();
            }
            continue;
        }
        switch (frame.type) {
        case TYPE_CMD:
            on_command(frame);
            break;
        case TYPE_DATA_IN:
            on_push_data(frame);
            break;
        case TYPE_PING:
            (void)send_frame(TYPE_HELLO, frame.id, reinterpret_cast<const uint8_t*>(hello), std::strlen(hello),
                             osal::WAIT_FOREVER);
            break;
        default:
            break; // ACK lạc (pull đã hủy) hoặc loại khung chưa hỗ trợ.
        }
    }
}

} // namespace

common::Status bridge_start(const hal::Devices& devices)
{
    if (s_is_running) {
        return common::Status::ERR_STATE;
    }
    if ((devices.p_debug_port == nullptr) || (devices.p_system == nullptr)) {
        return common::Status::ERR_NOT_SUPPORTED;
    }
    common::Status status = devices.p_debug_port->init();
    if (!common::is_ok(status)) {
        return status;
    }
    s_p_devices = &devices;
    s_p_port = devices.p_debug_port;
    status = osal::task_create(osal::TaskConfig{"gach_bridge", TASK_STACK_BYTES, TASK_PRIORITY, TASK_CORE},
                               &bridge_task, nullptr);
    s_is_running = common::is_ok(status);
    return status;
}

bool bridge_log(const char* p_text, size_t len)
{
    if (!s_is_running || !s_is_session) {
        return false; // Không có máy tính: người gọi in chữ thô như bình thường.
    }
    for (size_t offset = 0U; offset < len; offset += MAX_DATA) {
        const size_t chunk = ((len - offset) < MAX_DATA) ? (len - offset) : MAX_DATA;
        if (!send_frame(TYPE_LOG, 0U, reinterpret_cast<const uint8_t*>(&p_text[offset]), chunk, LOG_LOCK_TIMEOUT_MS)) {
            return true; // Bận (gọi lồng từ trong đường ghi): bỏ dòng log thay vì làm hỏng khung.
        }
    }
    return true;
}

} // namespace runtime
