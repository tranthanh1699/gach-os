/**
 * @file    system.hpp
 * @brief   HAL contract: thông tin & điều khiển hệ thống (chip, heap, reset).
 */
#ifndef HAL_API_SYSTEM_HPP
#define HAL_API_SYSTEM_HPP

#include <cstddef>
#include <cstdint>

#include "common/status.hpp"

namespace hal {

constexpr uint8_t SYSTEM_MAX_CORES = 2U; /**< Số core tối đa CpuStats theo dõi. */

/** @brief Thông tin tĩnh/động của nền tảng. */
struct SystemInfo {
    const char* p_chip_name;  /**< VD: "ESP32-D0WD". */
    uint32_t cpu_freq_mhz;    /**< Tần số CPU (MHz). */
    uint8_t cpu_cores;        /**< Số core. */
    uint32_t flash_size_kb;   /**< Dung lượng flash (KiB). */
    uint32_t heap_free_bytes; /**< Heap còn trống tại thời điểm đọc. */
    uint32_t heap_min_bytes;  /**< Heap thấp nhất từng ghi nhận (watermark). */
};

/** @brief Thống kê bộ nhớ heap (byte). PSRAM = 0 nếu board không có. */
struct MemoryStats {
    uint32_t internal_total;   /**< Tổng heap RAM nội. */
    uint32_t internal_free;    /**< Còn trống. */
    uint32_t internal_min;     /**< Thấp nhất từng ghi nhận. */
    uint32_t internal_largest; /**< Khối liền lớn nhất còn cấp được (độ phân mảnh). */
    uint32_t psram_total;      /**< Tổng heap PSRAM. */
    uint32_t psram_free;
    uint32_t psram_min;
};

/** @brief Tải CPU theo core, tính trên khoảng giữa hai lần đọc. */
struct CpuStats {
    uint8_t core_count;                  /**< Số core có số liệu. */
    uint8_t load_pct[SYSTEM_MAX_CORES];  /**< 0..100 (%). */
    uint16_t task_count;                 /**< Số task RTOS đang tồn tại. */
};

/** @brief Flash và firmware (byte). */
struct FirmwareStats {
    uint32_t flash_total;   /**< Dung lượng chip flash. */
    uint32_t app_partition; /**< Kích thước phân vùng app đang chạy. */
    uint32_t app_used;      /**< Kích thước image firmware trong phân vùng. */
};

// ============================================================================
// Introspection (debug bridge: sysinfo, ps, top, heap, memmap...) – mọi hàm mặc định "không hỗ trợ"
// ============================================================================
constexpr size_t SYSTEM_NAME_LEN = 32U; /**< Chuỗi tên / phiên bản (kể cả '\0'). */
constexpr size_t TASK_NAME_LEN = 16U;
constexpr size_t MAC_LEN = 6U;
constexpr size_t HEAP_TRACE_DEPTH = 4U; /**< Số cấp gọi ghi lại cho mỗi lần cấp phát. */

/** @brief Phần cứng chip. */
struct ChipDetails {
    char model[SYSTEM_NAME_LEN];  /**< VD "ESP32-P4". */
    char arch[SYSTEM_NAME_LEN];   /**< VD "RISC-V RV32IMAFC". */
    uint16_t revision;            /**< major * 100 + minor. */
    uint8_t cores;
    uint32_t cpu_mhz;             /**< Tần số đang chạy. */
    uint32_t flash_bytes;
    uint32_t psram_bytes;
    bool has_wifi;                /**< Wi-Fi trên chip (Wi-Fi qua chip phụ không tính). */
    bool has_bluetooth;
    bool has_ieee802154;
    bool has_mac;
    uint8_t mac[MAC_LEN];         /**< MAC gốc trong eFuse. */
};

/** @brief Firmware đang chạy. */
struct FirmwareInfo {
    char project[SYSTEM_NAME_LEN];
    char version[SYSTEM_NAME_LEN];     /**< Thường là `git describe` lúc build. */
    char build_date[SYSTEM_NAME_LEN];
    char build_time[SYSTEM_NAME_LEN];
    char sdk_version[SYSTEM_NAME_LEN]; /**< SDK của chip (VD "v6.1.1"). */
};

/** @brief Lý do khởi động lại. */
enum class ResetReason : uint8_t {
    UNKNOWN, POWER_ON, EXTERNAL, SOFTWARE, PANIC, INT_WATCHDOG, TASK_WATCHDOG, OTHER_WATCHDOG,
    DEEP_SLEEP, BROWNOUT, SDIO, USB, JTAG, EFUSE, POWER_GLITCH, CPU_LOCKUP
};

/** @brief Thông tin lần khởi động này. */
struct BootInfo {
    ResetReason reason;
    uint32_t boot_count; /**< Số lần khởi động kể từ lần cấp nguồn (giữ qua reset mềm / panic / watchdog). */
};

/** @brief Trạng thái task RTOS. */
enum class TaskState : uint8_t { RUNNING, READY, BLOCKED, SUSPENDED, DELETED, UNKNOWN };

/** @brief Một task RTOS. */
struct TaskInfo {
    char name[TASK_NAME_LEN];
    uint32_t id;              /**< Số thứ tự task (ổn định trong suốt đời task). */
    TaskState state;
    uint8_t priority;
    int8_t core;              /**< Core được ghim, -1 = không ghim. */
    uint32_t stack_free_min;  /**< Byte stack còn trống thấp nhất từng ghi nhận (high-water mark). */
    uint64_t runtime;         /**< Bộ đếm thời gian CPU (đơn vị của platform, so với total_runtime). */
};

/** @brief Một vùng heap theo khả năng (RAM nội, DMA, PSRAM...). */
struct HeapRegion {
    char name[TASK_NAME_LEN];
    uint32_t total;
    uint32_t free;
    uint32_t min_free;
    uint32_t largest;
    uint32_t allocated_blocks;
    uint32_t free_blocks;
};

/** @brief Một phân vùng trong flash. */
struct FlashPartition {
    char label[TASK_NAME_LEN + 1U];
    char kind[TASK_NAME_LEN];  /**< VD "app/ota_0", "data/nvs". */
    uint32_t offset;
    uint32_t size;
    bool is_running;           /**< Phân vùng app đang chạy. */
};

/** @brief Một khối heap còn sống trong bản ghi heap trace. */
struct HeapTraceRecord {
    uintptr_t address;
    uint32_t size;
    uint8_t core;
    uintptr_t callers[HEAP_TRACE_DEPTH]; /**< Địa chỉ nơi gọi (0 = không có). */
};

/** @brief Tóm tắt core dump của lần crash gần nhất (lưu trong flash, sống qua khởi động lại). */
struct CrashSummary {
    char task[TASK_NAME_LEN];   /**< Task gây lỗi. */
    char reason[64];            /**< Lý do panic (VD "Load access fault"), "" nếu không rõ. */
    uintptr_t pc;               /**< Địa chỉ lệnh gây lỗi. */
    uintptr_t return_address;   /**< Nơi gọi (RISC-V: thanh ghi ra), 0 nếu không có. */
    uint32_t cause;             /**< Mã nguyên nhân của CPU (RISC-V mcause). */
    uint32_t fault_address;     /**< Địa chỉ truy cập lỗi (RISC-V mtval). */
    uint32_t size;              /**< Kích thước core dump (byte). */
    char elf_sha[17];           /**< 16 ký tự đầu SHA256 của firmware đã crash (đối chiếu với file .elf). */
};

/** @brief Cấu hình watchdog. */
struct WatchdogInfo {
    bool task_enabled;
    uint32_t task_timeout_ms;
    bool task_panics;           /**< Hết hạn -> panic (reset) thay vì chỉ in cảnh báo. */
    bool task_idle_core[SYSTEM_MAX_CORES]; /**< Theo dõi task idle của core (phát hiện core bị chiếm). */
    bool interrupt_enabled;
    uint32_t interrupt_timeout_ms;
};

/** @brief Dịch vụ hệ thống do platform cung cấp. */
class ISystem {
public:
    virtual ~ISystem() = default;

    /** @param[out] info Thông tin hệ thống hiện tại. */
    virtual void get_info(SystemInfo& info) const = 0;

    /** @brief Khởi động lại MCU (không return). */
    virtual void restart() = 0;

    /** @param[out] stats Heap RAM nội / PSRAM. @return ERR_NOT_SUPPORTED nếu platform không hỗ trợ. */
    virtual common::Status get_memory_stats(MemoryStats& stats) const
    {
        (void)stats;
        return common::Status::ERR_NOT_SUPPORTED;
    }

    /**
     * @brief Tải CPU từng core kể từ lần đọc trước (lần đầu: kể từ lúc khởi động).
     * @note  Đọc dày hơn chu kỳ lấy mẫu nội bộ sẽ trả lại kết quả đã có (nhiều người gọi không làm nhiễu nhau).
     */
    virtual common::Status get_cpu_stats(CpuStats& stats)
    {
        (void)stats;
        return common::Status::ERR_NOT_SUPPORTED;
    }

    /** @param[out] stats Dung lượng flash, phân vùng app, kích thước firmware. */
    virtual common::Status get_firmware_stats(FirmwareStats& stats) const
    {
        (void)stats;
        return common::Status::ERR_NOT_SUPPORTED;
    }

    // ---- Introspection ----

    /** @param[out] details Chip, revision, MAC, bộ nhớ. */
    virtual common::Status get_chip_details(ChipDetails& details) const
    {
        (void)details;
        return common::Status::ERR_NOT_SUPPORTED;
    }

    /** @param[out] info Phiên bản firmware / SDK chip. */
    virtual common::Status get_firmware_info(FirmwareInfo& info) const
    {
        (void)info;
        return common::Status::ERR_NOT_SUPPORTED;
    }

    /** @param[out] info Lý do reset, số lần khởi động. */
    virtual common::Status get_boot_info(BootInfo& info) const
    {
        (void)info;
        return common::Status::ERR_NOT_SUPPORTED;
    }

    /**
     * @brief Chụp danh sách task.
     * @param[out] p_out         Mảng nhận (tối đa max).
     * @param[out] count         Số task chép được.
     * @param[out] total_runtime Tổng bộ đếm thời gian (để tính %CPU giữa hai lần chụp).
     */
    virtual common::Status get_tasks(TaskInfo* p_out, size_t max, size_t& count, uint64_t& total_runtime) const
    {
        (void)p_out;
        (void)max;
        count = 0U;
        total_runtime = 0U;
        return common::Status::ERR_NOT_SUPPORTED;
    }

    /** @brief Các vùng heap (RAM nội, DMA, PSRAM, IRAM...). */
    virtual common::Status get_heap_regions(HeapRegion* p_out, size_t max, size_t& count) const
    {
        (void)p_out;
        (void)max;
        count = 0U;
        return common::Status::ERR_NOT_SUPPORTED;
    }

    /** @brief Bảng phân vùng flash. */
    virtual common::Status get_partitions(FlashPartition* p_out, size_t max, size_t& count) const
    {
        (void)p_out;
        (void)max;
        count = 0U;
        return common::Status::ERR_NOT_SUPPORTED;
    }

    /**
     * @brief Bắt đầu ghi các khối heap được cấp mà chưa giải phóng (tìm rò rỉ).
     * @param[in] max_records Số bản ghi tối đa (platform có thể giới hạn thấp hơn).
     */
    virtual common::Status heap_trace_start(size_t max_records)
    {
        (void)max_records;
        return common::Status::ERR_NOT_SUPPORTED;
    }

    /** @brief Dừng ghi (bản ghi giữ lại để đọc). */
    virtual common::Status heap_trace_stop() { return common::Status::ERR_NOT_SUPPORTED; }

    /**
     * @brief Tóm tắt bản ghi.
     * @param[out] count  Số khối còn sống được ghi.
     * @param[out] bytes  Tổng byte của các khối đó.
     * @param[out] is_running Đang ghi hay đã dừng.
     */
    virtual common::Status heap_trace_status(size_t& count, size_t& bytes, bool& is_running) const
    {
        count = 0U;
        bytes = 0U;
        is_running = false;
        return common::Status::ERR_NOT_SUPPORTED;
    }

    /** @brief Đọc bản ghi thứ index (< count). */
    virtual common::Status heap_trace_get(size_t index, HeapTraceRecord& record) const
    {
        (void)index;
        (void)record;
        return common::Status::ERR_NOT_SUPPORTED;
    }

    // ---- Chẩn đoán lỗi ----

    /** @return ERR_NOT_FOUND nếu không có core dump, ERR_NOT_SUPPORTED nếu platform không lưu core dump. */
    virtual common::Status crash_summary(CrashSummary& summary) const
    {
        (void)summary;
        return common::Status::ERR_NOT_SUPPORTED;
    }

    /** @brief Đọc core dump thô (để tải về phân tích). @param[out] got Số byte đọc được (0 = hết). */
    virtual common::Status crash_read(size_t offset, uint8_t* p_buf, size_t len, size_t& got) const
    {
        (void)offset;
        (void)p_buf;
        (void)len;
        got = 0U;
        return common::Status::ERR_NOT_SUPPORTED;
    }

    /** @brief Xóa core dump đã lưu. */
    virtual common::Status crash_erase() { return common::Status::ERR_NOT_SUPPORTED; }

    /** @param[out] info Cấu hình watchdog. */
    virtual common::Status get_watchdog_info(WatchdogInfo& info) const
    {
        (void)info;
        return common::Status::ERR_NOT_SUPPORTED;
    }

    /** @param[out] celsius Nhiệt độ chip (°C). */
    virtual common::Status read_temperature(float& celsius)
    {
        (void)celsius;
        return common::Status::ERR_NOT_SUPPORTED;
    }
};

} // namespace hal

#endif /* HAL_API_SYSTEM_HPP */
