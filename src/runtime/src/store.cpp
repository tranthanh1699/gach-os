/**
 * @file    store.cpp
 * @brief   Kho ứng dụng trực tuyến: tải danh mục index.tsv, tải gói .gapp thẳng vào thẻ SD, kiểm SHA-256, cài.
 *
 * Máy chủ chỉ cần phục vụ tĩnh thư mục appstore/dist (VD GitHub Pages, xem .github/workflows/appstore.yml).
 * Định dạng index.tsv (tools/gapp_pack.py): dòng đầu "#gach-store<TAB>1", mỗi dòng sau một app:
 * id, name, version, sdk, size, sha256, file, icon, color (tách bằng TAB).
 * Mọi hàm chạy trên task UI; tải chạy trên task HTTP của platform (hal::IHttpClient, một kênh dùng chung với
 * gach.net của app).
 */
#include "store.hpp"

#include <cstdio>
#include <cstdlib>
#include <cstring>

#include "common/event.hpp"
#include "common/string_utils.hpp"
#include "hal_api/board.hpp"
#include "manifest.hpp"
#include "runtime/runtime.hpp"
#include "runtime/sdk_version.hpp"

namespace runtime {

namespace {

constexpr const char* URL_KEY = "store_url";
constexpr const char* INDEX_FILE = "index.tsv";
constexpr const char* INDEX_HEADER = "#gach-store\t1";
constexpr const char* INSTALL_DIR = "install";
constexpr const char* PART_SUFFIX = ".part";
constexpr size_t INDEX_BUF_LEN = 16U * 1024U;
constexpr size_t FIELD_COUNT = 12U;     /**< Số cột tối đa của một dòng index.tsv. */
constexpr size_t FIELD_COUNT_MIN = 9U;  /**< Danh mục cũ (chưa có mô tả / thể loại / ảnh chụp). */
constexpr const char* CACHE_DIR = "store_cache";
constexpr size_t GIMG_HEADER_LEN = 16U;
constexpr uint8_t GIMG_RGB565A8 = 1U;
constexpr uint8_t MAX_SHOTS = 3U;
constexpr size_t MESSAGE_LEN = 96U;
constexpr size_t PATH_LEN = 128U;
constexpr int32_t HTTP_OK = 200;
constexpr int HEX_BASE = 16;
constexpr size_t SHA_HEX_LEN = 64U;

hal::IHttpClient* s_p_http = nullptr;
hal::IFileSystem* s_p_fs = nullptr;
hal::IKeyValueStore* s_p_kv = nullptr;
char s_url[STORE_URL_LEN] = {};
StoreApp s_apps[STORE_MAX_APPS];
size_t s_count = 0U;
StoreState s_state = StoreState::IDLE;
size_t s_installing = 0U;
char s_message[MESSAGE_LEN] = {};
char s_index[INDEX_BUF_LEN + 1U]; // Bản sao danh mục để tách trường tại chỗ.
char s_part_path[PATH_LEN + sizeof(".part")] = {}; // Đường dẫn đích + đuôi ".part".
char s_final_path[PATH_LEN] = {};

void set_message(const char* p_fmt, const char* p_arg = "") __attribute__((format(printf, 1, 0)));
void set_message(const char* p_fmt, const char* p_arg)
{
    (void)std::snprintf(s_message, sizeof(s_message), p_fmt, p_arg);
}

common::Status fail(const char* p_fmt, const char* p_arg = "") __attribute__((format(printf, 1, 0)));
common::Status fail(const char* p_fmt, const char* p_arg)
{
    set_message(p_fmt, p_arg);
    s_state = StoreState::FAILED;
    EVENT_W("store", "%s", s_message);
    return common::Status::ERR_IO;
}

bool is_safe_file(const char* p_file)
{
    const size_t len = std::strlen(p_file);
    return (len > 0U) && (std::strchr(p_file, '/') == nullptr) && (std::strstr(p_file, "..") == nullptr);
}

/** @brief Tách một dòng (đã kết thúc '\0') thành tối đa FIELD_COUNT trường theo TAB. */
size_t split_tabs(char* p_line, char* p_fields[FIELD_COUNT])
{
    size_t count = 0U;
    p_fields[count++] = p_line;
    for (char* p = p_line; (*p != '\0') && (count < FIELD_COUNT); ++p) {
        if (*p == '\t') {
            *p = '\0';
            p_fields[count++] = p + 1;
        }
    }
    return count;
}

bool parse_line(char* p_line, StoreApp& out)
{
    char* fields[FIELD_COUNT] = {};
    const size_t count = split_tabs(p_line, fields);
    if (count < FIELD_COUNT_MIN) {
        return false;
    }
    out = StoreApp{};
    (void)common::copy_string(out.id, sizeof(out.id), fields[0]);
    (void)common::copy_string(out.name, sizeof(out.name), fields[1]);
    (void)common::copy_string(out.version, sizeof(out.version), fields[2]);
    out.sdk = static_cast<uint8_t>(std::strtoul(fields[3], nullptr, 10));
    out.size = static_cast<uint32_t>(std::strtoul(fields[4], nullptr, 10));
    (void)common::copy_string(out.sha256, sizeof(out.sha256), fields[5]);
    (void)common::copy_string(out.file, sizeof(out.file), fields[6]);
    (void)common::copy_string(out.icon, sizeof(out.icon), fields[7]);
    out.color = (fields[8][0] == '#') ? static_cast<uint32_t>(std::strtoul(&fields[8][1], nullptr, HEX_BASE)) : 0U;
    if (count == FIELD_COUNT) {
        (void)common::copy_string(out.description, sizeof(out.description), fields[9]);
        (void)common::copy_string(out.category, sizeof(out.category), fields[10]);
        const unsigned long shots = std::strtoul(fields[11], nullptr, 10);
        out.shots = static_cast<uint8_t>((shots > MAX_SHOTS) ? MAX_SHOTS : shots);
    }
    return (out.id[0] != '\0') && (std::strlen(out.sha256) == SHA_HEX_LEN) && is_safe_file(out.file);
}

void parse_index(const uint8_t* p_body, size_t len)
{
    std::memcpy(s_index, p_body, len);
    s_index[len] = '\0';
    s_count = 0U;
    char* p_save = nullptr;
    char* p_line = strtok_r(s_index, "\r\n", &p_save);
    if ((p_line == nullptr) || (std::strcmp(p_line, INDEX_HEADER) != 0)) {
        (void)fail("not a gach store index (%s)", INDEX_FILE);
        return;
    }
    size_t skipped = 0U;
    for (p_line = strtok_r(nullptr, "\r\n", &p_save); (p_line != nullptr) && (s_count < STORE_MAX_APPS);
         p_line = strtok_r(nullptr, "\r\n", &p_save)) {
        if (parse_line(p_line, s_apps[s_count])) {
            ++s_count;
        } else {
            ++skipped;
        }
    }
    (void)std::snprintf(s_message, sizeof(s_message), "%u app(s)%s", static_cast<unsigned>(s_count),
                        (skipped > 0U) ? ", some entries skipped" : "");
    s_state = StoreState::DONE;
}

/** @brief Gói đã tải: kiểm kích thước + SHA-256, đổi tên .part -> .gapp, cài. */
void finish_install(const hal::HttpResult& result)
{
    const StoreApp& app = s_apps[s_installing];
    uint8_t digest[hal::HTTP_SHA256_LEN] = {};
    char hex[SHA_HEX_LEN + 1U] = {};
    const bool has_digest = s_p_http->download_sha256(digest);
    for (size_t index = 0U; has_digest && (index < sizeof(digest)); ++index) {
        (void)std::snprintf(&hex[index * 2U], 3U, "%02x", digest[index]);
    }
    if (!common::is_ok(result.status) || (result.http_code != HTTP_OK)) {
        char code[16];
        (void)std::snprintf(code, sizeof(code), "%ld", static_cast<long>(result.http_code));
        (void)fail(common::is_ok(result.status) ? "download failed (HTTP %s)" : "download failed (network)%s",
                   common::is_ok(result.status) ? code : "");
    } else if ((result.body_len != app.size) || !has_digest || (std::strcmp(hex, app.sha256) != 0)) {
        (void)fail("%s: file corrupted (checksum mismatch)", app.name);
    } else if (!common::is_ok(s_p_fs->rename(s_part_path, s_final_path))) {
        (void)fail("cannot save %s on the SD card", app.file);
    } else {
        size_t installed = 0U;
        if (!common::is_ok(install_pending(installed)) || (installed == 0U)) {
            (void)fail("%s", last_error());
        } else {
            set_message("%s installed", app.name);
            s_state = StoreState::DONE;
            EVENT_I("store", "installed %s %s from store", app.id, app.version);
        }
    }
    if (s_state == StoreState::FAILED) {
        (void)s_p_fs->remove(s_part_path); // Không để lại gói tải dở.
    }
}

} // namespace

void store_init(const hal::Devices& devices)
{
    s_p_http = devices.p_http;
    s_p_fs = devices.p_fs;
    s_p_kv = devices.p_storage;
    if ((s_p_kv == nullptr) || !common::is_ok(s_p_kv->get_string(URL_KEY, s_url, sizeof(s_url))) ||
        (s_url[0] == '\0')) {
        (void)common::copy_string(s_url, sizeof(s_url), STORE_DEFAULT_URL);
    }
}

const char* store_url()
{
    return (s_url[0] != '\0') ? s_url : STORE_DEFAULT_URL;
}

bool store_set_url(const char* p_url)
{
    if ((p_url == nullptr) || (p_url[0] == '\0')) {
        (void)common::copy_string(s_url, sizeof(s_url), STORE_DEFAULT_URL);
    } else {
        const size_t len = std::strlen(p_url);
        const bool has_scheme = (std::strncmp(p_url, "http://", 7U) == 0) || (std::strncmp(p_url, "https://", 8U) == 0);
        if (!has_scheme || ((len + 2U) > sizeof(s_url))) {
            return false;
        }
        (void)std::snprintf(s_url, sizeof(s_url), "%s%s", p_url, (p_url[len - 1U] == '/') ? "" : "/");
    }
    if (s_p_kv != nullptr) {
        (void)s_p_kv->set_string(URL_KEY, (std::strcmp(s_url, STORE_DEFAULT_URL) == 0) ? "" : s_url);
    }
    return true;
}

common::Status store_refresh()
{
    if (s_p_http == nullptr) {
        set_message("no network on this device");
        return common::Status::ERR_NOT_SUPPORTED;
    }
    if ((s_state == StoreState::LOADING_INDEX) || (s_state == StoreState::DOWNLOADING)) {
        return common::Status::ERR_STATE;
    }
    char url[STORE_URL_LEN + sizeof("index.tsv")];
    (void)std::snprintf(url, sizeof(url), "%s%s", store_url(), INDEX_FILE);
    const common::Status status = s_p_http->start_get(url);
    if (!common::is_ok(status)) {
        set_message((status == common::Status::ERR_STATE) ? "network busy, try again" : "bad store address");
        return status;
    }
    s_state = StoreState::LOADING_INDEX;
    s_message[0] = '\0';
    return common::Status::OK;
}

common::Status store_install(size_t index)
{
    if ((index >= s_count) || (s_p_http == nullptr) || (s_p_fs == nullptr) || !s_p_fs->is_mounted()) {
        set_message("no SD card");
        return common::Status::ERR_STATE;
    }
    if ((s_state == StoreState::LOADING_INDEX) || (s_state == StoreState::DOWNLOADING)) {
        return common::Status::ERR_STATE;
    }
    const StoreApp& app = s_apps[index];
    if (app.sdk > SDK_VERSION) {
        set_message("%s needs a newer firmware", app.name);
        return common::Status::ERR_NOT_SUPPORTED;
    }
    char url[STORE_URL_LEN + sizeof(app.file)];
    (void)std::snprintf(url, sizeof(url), "%s%s", store_url(), app.file);
    (void)s_p_fs->make_dir(INSTALL_DIR);
    (void)std::snprintf(s_final_path, sizeof(s_final_path), "%s/%s", INSTALL_DIR, app.file);
    (void)std::snprintf(s_part_path, sizeof(s_part_path), "%s/%s%s", INSTALL_DIR, app.file, PART_SUFFIX);
    const common::Status status = s_p_http->start_download(url, *s_p_fs, s_part_path);
    if (!common::is_ok(status)) {
        set_message((status == common::Status::ERR_STATE) ? "network busy, try again" : "download not supported");
        return status;
    }
    s_installing = index;
    s_state = StoreState::DOWNLOADING;
    s_message[0] = '\0';
    return common::Status::OK;
}

StoreState store_poll()
{
    const bool is_busy = (s_state == StoreState::LOADING_INDEX) || (s_state == StoreState::DOWNLOADING) ||
                         (s_state == StoreState::FETCHING_SHOT);
    if (!is_busy || (s_p_http->state() != hal::HttpState::DONE)) {
        return s_state;
    }
    const hal::HttpResult result = s_p_http->result();
    if (s_state == StoreState::LOADING_INDEX) {
        if (!common::is_ok(result.status)) {
            (void)fail("cannot reach the store (check Wi-Fi)");
        } else if (result.http_code != HTTP_OK) {
            char code[16];
            (void)std::snprintf(code, sizeof(code), "%ld", static_cast<long>(result.http_code));
            (void)fail("store returned HTTP %s", code);
        } else if (result.truncated || (result.body_len > INDEX_BUF_LEN)) {
            (void)fail("store index too large");
        } else {
            parse_index(s_p_http->body(), result.body_len);
        }
    } else if (s_state == StoreState::FETCHING_SHOT) {
        const bool ok = common::is_ok(result.status) && (result.http_code == HTTP_OK) &&
                        common::is_ok(s_p_fs->rename(s_part_path, s_final_path));
        if (!ok) {
            (void)s_p_fs->remove(s_part_path);
        }
        s_state = ok ? StoreState::DONE : StoreState::FAILED; // Ảnh lỗi: không ghi đè thông báo của kho.
    } else {
        finish_install(result);
    }
    s_p_http->release();
    return s_state;
}

void store_progress(size_t& received, size_t& total)
{
    received = 0U;
    total = 0U;
    if ((s_p_http != nullptr) && (s_state == StoreState::DOWNLOADING)) {
        s_p_http->download_progress(received, total);
        total = (total == 0U) ? s_apps[s_installing].size : total;
    }
}

size_t store_count()
{
    return s_count;
}

const StoreApp& store_app(size_t index)
{
    return s_apps[(index < s_count) ? index : 0U];
}

const char* store_message()
{
    return s_message;
}

namespace {

bool shot_path(size_t index, uint8_t n, char* p_out, size_t len)
{
    if ((index >= s_count) || (n == 0U) || (n > s_apps[index].shots)) {
        return false;
    }
    const StoreApp& app = s_apps[index];
    const int written = std::snprintf(p_out, len, "%s/%s-%s-%u.gimg", CACHE_DIR, app.id, app.version,
                                      static_cast<unsigned>(n));
    return (written > 0) && (static_cast<size_t>(written) < len);
}

uint16_t read_u16(const uint8_t* p_bytes)
{
    return static_cast<uint16_t>(p_bytes[0] | (static_cast<uint16_t>(p_bytes[1]) << 8U));
}

} // namespace

const char* app_icon_symbol(const char* p_name)
{
    return icon_symbol((p_name != nullptr) ? p_name : "");
}

bool store_shot_cached(size_t index, uint8_t n)
{
    char path[PATH_LEN];
    bool is_dir = false;
    return (s_p_fs != nullptr) && shot_path(index, n, path, sizeof(path)) && s_p_fs->exists(path, is_dir) && !is_dir;
}

common::Status store_fetch_shot(size_t index, uint8_t n)
{
    if ((s_p_http == nullptr) || (s_p_fs == nullptr) || !s_p_fs->is_mounted() ||
        !shot_path(index, n, s_final_path, sizeof(s_final_path))) {
        return common::Status::ERR_PARAM;
    }
    if ((s_state == StoreState::LOADING_INDEX) || (s_state == StoreState::DOWNLOADING) ||
        (s_state == StoreState::FETCHING_SHOT)) {
        return common::Status::ERR_STATE;
    }
    (void)s_p_fs->make_dir(CACHE_DIR);
    (void)std::snprintf(s_part_path, sizeof(s_part_path), "%s%s", s_final_path, PART_SUFFIX);
    char url[STORE_URL_LEN + PATH_LEN];
    (void)std::snprintf(url, sizeof(url), "%sshots/%s-%u.gimg", store_url(), s_apps[index].id, static_cast<unsigned>(n));
    const common::Status status = s_p_http->start_download(url, *s_p_fs, s_part_path);
    if (common::is_ok(status)) {
        s_state = StoreState::FETCHING_SHOT;
    }
    return status;
}

bool store_read_shot(size_t index, uint8_t n, uint8_t* p_buf, size_t cap, StoreImage& out)
{
    char path[PATH_LEN];
    hal::FileHandle handle = hal::INVALID_FILE;
    if ((p_buf == nullptr) || (cap < GIMG_HEADER_LEN) || !shot_path(index, n, path, sizeof(path)) ||
        !common::is_ok(s_p_fs->open(path, hal::OpenMode::READ, handle))) {
        return false;
    }
    size_t got = 0U;
    const bool ok = common::is_ok(s_p_fs->read(handle, p_buf, cap, got));
    s_p_fs->close(handle);
    if (!ok || (got < GIMG_HEADER_LEN) || (std::memcmp(p_buf, "GIMG", 4U) != 0)) {
        return false;
    }
    out.width = read_u16(&p_buf[8]);
    out.height = read_u16(&p_buf[10]);
    out.stride = read_u16(&p_buf[12]);
    out.has_alpha = (p_buf[5] == GIMG_RGB565A8);
    out.data_size = static_cast<uint32_t>(out.height) * out.stride +
                    (out.has_alpha ? (static_cast<uint32_t>(out.width) * out.height) : 0U);
    out.p_data = &p_buf[GIMG_HEADER_LEN];
    return (got == (GIMG_HEADER_LEN + out.data_size)) && (out.width > 0U) && (out.height > 0U);
}

} // namespace runtime
