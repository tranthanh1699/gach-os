#!/usr/bin/env bash
# ==============================================================================
# Build backend cho họ chip ESP32 (ESP-IDF). Được gọi bởi <repo>/build.sh.
#
#   platform/esp32/build.sh <board> <action> [args idf.py bổ sung...]
#
# Yêu cầu: môi trường ESP-IDF đã export (idf.py trong PATH) – native hoặc trong Docker.
# ==============================================================================
set -euo pipefail

PLATFORM_DIR="$(cd "$(dirname "${BASH_SOURCE[0]}")" && pwd)"
REPO_DIR="$(cd "${PLATFORM_DIR}/../.." && pwd)"

BOARD="${1:?Thiếu tên board}"
ACTION="${2:-build}"
shift $(( $# >= 2 ? 2 : $# ))

BOARD_DIR="${PLATFORM_DIR}/boards/${BOARD}"
[[ -f "${BOARD_DIR}/board.env" ]] || { echo "[esp32] Board '${BOARD}' không tồn tại" >&2; exit 2; }
# shellcheck source=/dev/null
source "${BOARD_DIR}/board.env"

command -v idf.py >/dev/null 2>&1 || {
    echo "[esp32] Không tìm thấy idf.py. Chạy qua Docker: ./build.sh --docker ..." >&2
    exit 3
}

BUILD_DIR="${REPO_DIR}/build/${BOARD}"
IDF_ARGS=(
    -C "${PLATFORM_DIR}"
    -B "${BUILD_DIR}"
    -DBOARD="${BOARD}"
    -DIDF_TARGET="${IDF_TARGET}"
    -DSDKCONFIG="${BUILD_DIR}/sdkconfig"
    -DSDKCONFIG_DEFAULTS="${PLATFORM_DIR}/sdkconfig.defaults;${BOARD_DIR}/sdkconfig.defaults"
)
[[ -n "${PORT:-}" ]] && IDF_ARGS+=(-p "${PORT}")

case "${ACTION}" in
    build)         idf.py "${IDF_ARGS[@]}" "$@" build ;;
    flash)         idf.py "${IDF_ARGS[@]}" "$@" flash ;;
    monitor)       idf.py "${IDF_ARGS[@]}" "$@" monitor ;;
    flash-monitor) idf.py "${IDF_ARGS[@]}" "$@" flash monitor ;;
    menuconfig)    idf.py "${IDF_ARGS[@]}" "$@" menuconfig ;;
    size)          idf.py "${IDF_ARGS[@]}" "$@" size-components ;;
    clean)         idf.py "${IDF_ARGS[@]}" "$@" clean ;;
    fullclean)     rm -rf "${BUILD_DIR}" && echo "[esp32] Đã xóa ${BUILD_DIR}" ;;
    *) echo "[esp32] Action không hỗ trợ: ${ACTION}" >&2; exit 2 ;;
esac
