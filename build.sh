#!/usr/bin/env bash
# ==============================================================================
# gach-os – điểm vào build duy nhất.
#
#   ./build.sh [options] [action]
#
# Actions : build (mặc định) | flash | monitor | flash-monitor | menuconfig | size
#           clean | fullclean | check (chỉ kiểm tra layer matrix) | shell (Docker)
# Options : -b, --board <name>  Board đích (mặc định: $BOARD hoặc esp32_2432s028)
#           -p, --port <dev>    Cổng serial khi flash/monitor (VD: /dev/ttyUSB0)
#           -d, --docker        Bắt buộc chạy trong Docker
#           -n, --native        Bắt buộc dùng ESP-IDF cài trên máy
#           -l, --list          Liệt kê board hỗ trợ
#           -h, --help
#
# Luồng: layer check (host) -> tìm platform chứa board -> platform/<chip>/build.sh
# Mặc định: dùng idf.py nếu có trong PATH, ngược lại tự chuyển sang Docker.
# ==============================================================================
set -euo pipefail

REPO_DIR="$(cd "$(dirname "${BASH_SOURCE[0]}")" && pwd)"
DOCKER_IMAGE="${DOCKER_IMAGE:-gach-os-idf:latest}"
BOARD="${BOARD:-esp32_2432s028}"
PORT="${PORT:-}"
MODE="auto"
ACTION="build"

log()  { printf '\033[1;34m[build]\033[0m %s\n' "$*"; }
die()  { printf '\033[1;31m[build]\033[0m %s\n' "$*" >&2; exit 1; }
usage() { sed -n '3,19p' "${BASH_SOURCE[0]}" | sed 's/^# \{0,1\}//'; }

list_boards() {
    local env
    for env in "${REPO_DIR}"/platform/*/boards/*/board.env; do
        [[ -f "${env}" ]] || continue
        local board_dir platform desc
        board_dir="$(dirname "${env}")"
        platform="$(basename "$(dirname "$(dirname "${board_dir}")")")"
        desc="$(grep -E '^BOARD_DESCRIPTION=' "${env}" | cut -d= -f2- | tr -d '"')"
        printf '  %-20s [%s] %s\n' "$(basename "${board_dir}")" "${platform}" "${desc}"
    done
}

find_platform() {
    local matches=("${REPO_DIR}"/platform/*/boards/"${BOARD}"/board.env)
    [[ -f "${matches[0]}" ]] || die "Board '${BOARD}' không tồn tại. Dùng --list để xem danh sách."
    basename "$(dirname "$(dirname "$(dirname "${matches[0]}")")")"
}

run_layer_check() {
    command -v python3 >/dev/null 2>&1 || die "Cần python3 để chạy layer check."
    python3 "${REPO_DIR}/tools/layer_check.py" --root "${REPO_DIR}"
}

ensure_docker_image() {
    command -v docker >/dev/null 2>&1 || die "Không có docker và không có idf.py – không thể build."
    if ! docker info >/dev/null 2>&1; then
        if getent group docker | grep -qw "$(id -un)" && ! id -nG | grep -qw docker; then
            die "User đã thuộc group 'docker' nhưng phiên hiện tại chưa nhận. Chạy 'newgrp docker' hoặc đăng xuất/đăng nhập lại."
        fi
        die "Không kết nối được Docker daemon: 'sudo usermod -aG docker \$USER' rồi đăng nhập lại, hoặc 'sudo systemctl start docker'."
    fi
    if ! docker image inspect "${DOCKER_IMAGE}" >/dev/null 2>&1; then
        log "Build Docker image ${DOCKER_IMAGE}..."
        docker build -t "${DOCKER_IMAGE}" -f "${REPO_DIR}/docker/Dockerfile" "${REPO_DIR}/docker"
    fi
}

run_in_docker() {
    ensure_docker_image
    local args=(--rm -v "${REPO_DIR}:/project" -w /project
                -u "$(id -u):$(id -g)" -e HOME=/tmp -e BOARD="${BOARD}" -e PORT="${PORT}"
                -e CCACHE_DIR=/project/build/.ccache)  # cache nằm trong bind-mount -> đúng owner
    [[ -t 0 && -t 1 ]] && args+=(-it)
    # --group-add: user trong container cần quyền group sở hữu cổng serial (thường là dialout).
    [[ -n "${PORT}" ]] && args+=(--device "${PORT}" --group-add "$(stat -c %g "${PORT}" 2>/dev/null || echo 20)")
    docker run "${args[@]}" "${DOCKER_IMAGE}" "$@"
}

# ---- Parse arguments ----
while [[ $# -gt 0 ]]; do
    case "$1" in
        -b|--board)  BOARD="${2:?}"; shift 2 ;;
        -p|--port)   PORT="${2:?}"; shift 2 ;;
        -d|--docker) MODE="docker"; shift ;;
        -n|--native) MODE="native"; shift ;;
        -l|--list)   list_boards; exit 0 ;;
        -h|--help)   usage; exit 0 ;;
        -*)          die "Option không hợp lệ: $1" ;;
        *)           ACTION="$1"; shift ;;
    esac
done
export BOARD PORT

if [[ "${MODE}" == "auto" ]]; then
    if command -v idf.py >/dev/null 2>&1; then MODE="native"; else MODE="docker"; fi
fi

case "${ACTION}" in
    check) run_layer_check; exit 0 ;;
    shell) run_in_docker bash; exit 0 ;;
    build|flash|flash-monitor|size) run_layer_check ;;
    monitor|menuconfig|clean|fullclean) ;;
    *) die "Action không hợp lệ: ${ACTION} (xem --help)" ;;
esac

if [[ "${MODE}" == "docker" && -z "${PORT}" && "${ACTION}" =~ ^(flash|monitor|flash-monitor)$ ]]; then
    die "Chạy trong Docker cần chỉ định cổng serial: ./build.sh -p /dev/ttyUSB0 ${ACTION}"
fi
[[ -z "${PORT}" || -e "${PORT}" ]] || die "Không thấy cổng ${PORT}. Kiểm tra cáp USB / 'ls /dev/ttyUSB* /dev/ttyACM*'."

PLATFORM="$(find_platform)"
PLATFORM_SCRIPT="platform/${PLATFORM}/build.sh"
[[ -x "${REPO_DIR}/${PLATFORM_SCRIPT}" ]] || die "Thiếu ${PLATFORM_SCRIPT} (hoặc chưa chmod +x)."

log "board=${BOARD} platform=${PLATFORM} action=${ACTION} mode=${MODE}"
if [[ "${MODE}" == "docker" ]]; then
    run_in_docker "./${PLATFORM_SCRIPT}" "${BOARD}" "${ACTION}"
else
    "${REPO_DIR}/${PLATFORM_SCRIPT}" "${BOARD}" "${ACTION}"
fi
