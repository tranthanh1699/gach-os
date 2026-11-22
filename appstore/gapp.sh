#!/usr/bin/env bash
# ==============================================================================
# gach-os appstore – phát triển, đóng gói và cài app (.gapp).
#
#   ./appstore/gapp.sh new <tên> [--id org.x.y] [--name "Tên hiển thị"]   Tạo app mới từ template
#   ./appstore/gapp.sh check [app...]                                     Kiểm tra manifest + cú pháp
#   ./appstore/gapp.sh pack  [app...]                                     Đóng gói -> appstore/dist/*.gapp + index.json
#   ./appstore/gapp.sh install <thư mục thẻ SD> [app...]                  Đóng gói rồi chép vào <SD>/install/
#   ./appstore/gapp.sh list                                               Liệt kê app trong kho
#
# Không truyền [app...] = áp dụng cho mọi app trong appstore/apps/.
# VD: ./appstore/gapp.sh install /media/$USER/SDCARD calculator
# ==============================================================================
set -euo pipefail

STORE_DIR="$(cd "$(dirname "${BASH_SOURCE[0]}")" && pwd)"
REPO_DIR="$(cd "${STORE_DIR}/.." && pwd)"
APPS_DIR="${STORE_DIR}/apps"
DIST_DIR="${STORE_DIR}/dist"
TEMPLATE_DIR="${STORE_DIR}/template"
PACKER="${REPO_DIR}/tools/gapp_pack.py"

log() { printf '\033[1;34m[gapp]\033[0m %s\n' "$*"; }
die() { printf '\033[1;31m[gapp]\033[0m %s\n' "$*" >&2; exit 1; }
usage() { sed -n '3,13p' "${BASH_SOURCE[0]}" | sed 's/^# \{0,1\}//'; }

command -v python3 >/dev/null 2>&1 || die "Cần python3."

# Danh sách thư mục app -> mảng APPS (chạy trong shell chính để `die` dừng được script).
APPS=()
resolve_apps() {
    APPS=()
    if [[ $# -eq 0 ]]; then
        local dir
        for dir in "${APPS_DIR}"/*/; do
            [[ -f "${dir}manifest.ini" ]] && APPS+=("${dir%/}")
        done
        return 0
    fi
    local name
    for name in "$@"; do
        if [[ -f "${name}/manifest.ini" ]]; then
            APPS+=("$(cd "${name}" && pwd)")
        elif [[ -f "${APPS_DIR}/${name}/manifest.ini" ]]; then
            APPS+=("${APPS_DIR}/${name}")
        else
            die "Không tìm thấy app '${name}' (thư mục cần có manifest.ini)."
        fi
    done
}

manifest_value() { grep -E "^$2=" "$1/manifest.ini" | head -1 | cut -d= -f2- | sed 's/[[:space:]]*$//'; }

cmd_new() {
    local name="${1:-}"; shift || true
    [[ -n "${name}" ]] || die "Thiếu tên: ./appstore/gapp.sh new <tên> [--id ...] [--name ...]"
    [[ "${name}" =~ ^[a-z0-9_-]+$ ]] || die "Tên thư mục chỉ gồm [a-z0-9_-]."
    local app_id="org.gachos.${name//_/-}" display="${name}"
    while [[ $# -gt 0 ]]; do
        case "$1" in
            --id)   app_id="${2:?}"; shift 2 ;;
            --name) display="${2:?}"; shift 2 ;;
            *)      die "Option không hợp lệ: $1" ;;
        esac
    done
    local target="${APPS_DIR}/${name}"
    [[ ! -e "${target}" ]] || die "${target} đã tồn tại."
    mkdir -p "${target}"
    local file
    for file in "${TEMPLATE_DIR}"/*; do
        sed -e "s/__ID__/${app_id}/g" -e "s/__NAME__/${display}/g" "${file}" > "${target}/$(basename "${file}")"
    done
    python3 "${PACKER}" "${target}" --check >/dev/null
    log "Đã tạo ${target} (id=${app_id}). Sửa main.lua rồi: ./appstore/gapp.sh install <SD> ${name}"
}

cmd_check() {
    resolve_apps "$@"
    local app
    for app in "${APPS[@]}"; do python3 "${PACKER}" "${app}" --check; done
}

cmd_pack() {
    resolve_apps "$@"
    [[ ${#APPS[@]} -gt 0 ]] || die "Không có app nào để đóng gói."
    mkdir -p "${DIST_DIR}"
    # Đóng gói toàn kho -> dọn gói cũ để index.json không chứa app đã xóa.
    [[ $# -eq 0 ]] && rm -f "${DIST_DIR}"/*.gapp
    local app
    for app in "${APPS[@]}"; do
        python3 "${PACKER}" "${app}" -o "${DIST_DIR}/$(manifest_value "${app}" id).gapp"
    done
    python3 "${PACKER}" --index "${DIST_DIR}" >/dev/null
    log "${#APPS[@]} gói -> ${DIST_DIR} (kèm index.json)"
}

cmd_install() {
    local sd="${1:-}"; shift || true
    [[ -n "${sd}" && -d "${sd}" ]] || die "Thư mục thẻ SD không tồn tại: '${sd}' (VD: /media/${USER}/SDCARD)"
    cmd_pack "$@"
    mkdir -p "${sd}/install"
    local app id
    for app in "${APPS[@]}"; do
        id="$(manifest_value "${app}" id)"
        cp "${DIST_DIR}/${id}.gapp" "${sd}/install/"
        log "Chép ${id}.gapp -> ${sd}/install/"
    done
    sync
    log "Xong. Cắm thẻ vào máy, khởi động lại hoặc mở app 'Apps' -> nút thẻ SD."
}

cmd_list() {
    local app
    printf '%-14s %-24s %-12s %-8s %s\n' "THƯ MỤC" "ID" "TÊN" "VERSION" "SDK"
    resolve_apps
    for app in "${APPS[@]}"; do
        printf '%-14s %-24s %-12s %-8s %s\n' "$(basename "${app}")" "$(manifest_value "${app}" id)" \
            "$(manifest_value "${app}" name)" "$(manifest_value "${app}" version)" "$(manifest_value "${app}" sdk)"
    done
}

case "${1:-}" in
    new)     shift; cmd_new "$@" ;;
    check)   shift; cmd_check "$@" ;;
    pack)    shift; cmd_pack "$@" ;;
    install) shift; cmd_install "$@" ;;
    list)    shift; cmd_list ;;
    -h|--help|"") usage ;;
    *) die "Lệnh không hợp lệ: $1 (xem --help)" ;;
esac
