/**
 * @file    manifest.cpp
 * @brief   Hiện thực đọc manifest.ini.
 */
#include "manifest.hpp"

#include <cstring>

#include "common/string_utils.hpp"
#include "lvgl.h"
#include "runtime/sdk_version.hpp"

namespace runtime {

namespace {

constexpr size_t LINE_BUF_LEN = 96U;
constexpr uint32_t DEFAULT_COLOR = 0x6E7681U;
constexpr uint32_t HEX_BASE = 16U;
constexpr uint32_t DECIMAL_BASE = 10U;

struct IconEntry {
    const char* p_name;
    const char* p_symbol;
};

const IconEntry ICONS[] = {
    {"audio", LV_SYMBOL_AUDIO}, {"video", LV_SYMBOL_VIDEO},     {"list", LV_SYMBOL_LIST},
    {"settings", LV_SYMBOL_SETTINGS}, {"home", LV_SYMBOL_HOME}, {"image", LV_SYMBOL_IMAGE},
    {"play", LV_SYMBOL_PLAY},   {"bell", LV_SYMBOL_BELL},       {"gps", LV_SYMBOL_GPS},
    {"file", LV_SYMBOL_FILE},   {"edit", LV_SYMBOL_EDIT},       {"envelope", LV_SYMBOL_ENVELOPE},
    {"call", LV_SYMBOL_CALL},   {"keyboard", LV_SYMBOL_KEYBOARD}, {"charge", LV_SYMBOL_CHARGE},
    {"folder", LV_SYMBOL_DIRECTORY}, {"tint", LV_SYMBOL_TINT}, {"eye", LV_SYMBOL_EYE_OPEN},
    {"wifi", LV_SYMBOL_WIFI},   {"shuffle", LV_SYMBOL_SHUFFLE}, {"loop", LV_SYMBOL_LOOP},
};

bool parse_uint(const char* p_text, uint32_t base, uint32_t& value)
{
    value = 0U;
    if (*p_text == '\0') {
        return false;
    }
    for (; *p_text != '\0'; ++p_text) {
        const char ch = *p_text;
        uint32_t digit = 0U;
        if ((ch >= '0') && (ch <= '9')) {
            digit = static_cast<uint32_t>(ch - '0');
        } else if ((base == HEX_BASE) && (ch >= 'a') && (ch <= 'f')) {
            digit = static_cast<uint32_t>(ch - 'a') + 10U;
        } else if ((base == HEX_BASE) && (ch >= 'A') && (ch <= 'F')) {
            digit = static_cast<uint32_t>(ch - 'A') + 10U;
        } else {
            return false;
        }
        value = (value * base) + digit;
    }
    return true;
}

char* trim(char* p_text)
{
    while ((*p_text == ' ') || (*p_text == '\t')) {
        ++p_text;
    }
    size_t len = std::strlen(p_text);
    while ((len > 0U) && ((p_text[len - 1U] == ' ') || (p_text[len - 1U] == '\t') || (p_text[len - 1U] == '\r'))) {
        p_text[--len] = '\0';
    }
    return p_text;
}

uint8_t parse_permissions(const char* p_value)
{
    uint8_t mask = PERMISSION_NONE;
    if (std::strstr(p_value, "storage") != nullptr) {
        mask |= PERMISSION_STORAGE;
    }
    if (std::strstr(p_value, "network") != nullptr) {
        mask |= PERMISSION_NETWORK;
    }
    return mask;
}

void apply_key(Manifest& out, const char* p_key, const char* p_value, bool& has_sdk)
{
    uint32_t number = 0U;
    if (std::strcmp(p_key, "id") == 0) {
        (void)common::copy_string(out.id, sizeof(out.id), p_value);
    } else if (std::strcmp(p_key, "name") == 0) {
        (void)common::copy_string(out.name, sizeof(out.name), p_value);
    } else if (std::strcmp(p_key, "version") == 0) {
        (void)common::copy_string(out.version, sizeof(out.version), p_value);
    } else if (std::strcmp(p_key, "entry") == 0) {
        (void)common::copy_string(out.entry, sizeof(out.entry), p_value);
    } else if (std::strcmp(p_key, "icon") == 0) {
        (void)common::copy_string(out.icon, sizeof(out.icon), p_value);
    } else if ((std::strcmp(p_key, "color") == 0) && (p_value[0] == '#') && parse_uint(&p_value[1], HEX_BASE, number)) {
        out.color_hex = number;
    } else if ((std::strcmp(p_key, "sdk") == 0) && parse_uint(p_value, DECIMAL_BASE, number) && (number <= UINT8_MAX)) {
        out.sdk = static_cast<uint8_t>(number);
        has_sdk = true;
    } else if (std::strcmp(p_key, "permissions") == 0) {
        out.permissions = parse_permissions(p_value);
    } else {
        // Khóa lạ: bỏ qua để manifest của SDK mới hơn vẫn đọc được phần cũ.
    }
}

} // namespace

bool is_valid_app_id(const char* p_id)
{
    const size_t len = std::strlen(p_id);
    if ((len == 0U) || (len >= APP_ID_BUF_LEN) || (p_id[0] == '.')) {
        return false;
    }
    for (size_t index = 0U; index < len; ++index) {
        const char ch = p_id[index];
        const bool ok = ((ch >= 'a') && (ch <= 'z')) || ((ch >= '0') && (ch <= '9')) || (ch == '.') || (ch == '_') ||
                        (ch == '-');
        if (!ok) {
            return false;
        }
    }
    return std::strstr(p_id, "..") == nullptr;
}

common::Status parse_manifest(const char* p_text, size_t len, Manifest& out, const char** p_err)
{
    out = Manifest{};
    out.color_hex = DEFAULT_COLOR;
    (void)common::copy_string(out.entry, sizeof(out.entry), "main.lua");
    bool has_sdk = false;

    size_t pos = 0U;
    while (pos < len) {
        char line[LINE_BUF_LEN] = {};
        size_t line_len = 0U;
        while ((pos < len) && (p_text[pos] != '\n')) {
            if ((line_len + 1U) < sizeof(line)) {
                line[line_len++] = p_text[pos];
            }
            ++pos;
        }
        ++pos; // Bỏ '\n'.
        char* const p_line = trim(line);
        char* const p_eq = std::strchr(p_line, '=');
        if ((p_line[0] == '#') || (p_line[0] == '\0') || (p_eq == nullptr)) {
            continue;
        }
        *p_eq = '\0';
        apply_key(out, trim(p_line), trim(p_eq + 1), has_sdk);
    }

    const char* p_error = nullptr;
    if (!is_valid_app_id(out.id)) {
        p_error = "invalid or missing id";
    } else if ((out.name[0] == '\0') || (out.version[0] == '\0')) {
        p_error = "missing name/version";
    } else if (!has_sdk || (out.sdk == 0U)) {
        p_error = "missing sdk";
    } else if (out.sdk > SDK_VERSION) {
        p_error = "requires newer SDK";
    } else if ((std::strchr(out.entry, '/') != nullptr) || (std::strstr(out.entry, "..") != nullptr)) {
        p_error = "invalid entry";
    } else {
        return common::Status::OK;
    }
    if (p_err != nullptr) {
        *p_err = p_error;
    }
    return common::Status::ERR_PARAM;
}

const char* icon_symbol(const char* p_name)
{
    for (const IconEntry& entry : ICONS) {
        if (std::strcmp(entry.p_name, p_name) == 0) {
            return entry.p_symbol;
        }
    }
    return LV_SYMBOL_FILE;
}

} // namespace runtime
