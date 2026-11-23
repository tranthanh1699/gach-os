# syntax=docker/dockerfile:1.4
# ==============================================================================
# Môi trường build tái lập cho gach-os: ESP-IDF chính thức + ccache.
#   docker build -t gach-os-idf:latest -f docker/Dockerfile docker
# (./build.sh tự build image khi cần)
# ==============================================================================
# v6.1.1: có workaround lỗi phần cứng PPA SRM của ESP32-P4 (DIG-734, PPA treo vĩnh viễn khi xoay một số khối).
ARG IDF_VERSION=v6.1.1
FROM espressif/idf:${IDF_VERSION}

LABEL org.opencontainers.image.title="gach-os-idf" \
      org.opencontainers.image.description="ESP-IDF build environment for gach-os"

ENV DEBIAN_FRONTEND=noninteractive \
    IDF_CCACHE_ENABLE=1 \
    CCACHE_DIR=/project/build/.ccache \
    HOME=/tmp

RUN --mount=type=cache,target=/var/cache/apt,sharing=locked \
    --mount=type=cache,target=/var/lib/apt,sharing=locked \
    apt-get update && apt-get install -y --no-install-recommends ccache clang-format \
    && git config --system --add safe.directory '*'

WORKDIR /project
# ENTRYPOINT của image gốc (/opt/esp/entrypoint.sh) đã source export.sh -> idf.py có sẵn.
CMD ["bash"]
