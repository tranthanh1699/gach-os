# syntax=docker/dockerfile:1.4
# ==============================================================================
# Môi trường build tái lập cho esp32-os: ESP-IDF chính thức + ccache.
#   docker build -t esp32-os-idf:latest -f docker/Dockerfile docker
# (./build.sh tự build image khi cần)
# ==============================================================================
ARG IDF_VERSION=v5.4.1
FROM espressif/idf:${IDF_VERSION}

LABEL org.opencontainers.image.title="esp32-os-idf" \
      org.opencontainers.image.description="ESP-IDF build environment for esp32-os"

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
