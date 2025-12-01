FROM mcr.microsoft.com/devcontainers/cpp:2.1.2-bookworm

RUN \
    --mount=type=cache,target=/var/lib/apt,sharing=locked \
    --mount=type=cache,target=/var/cache/apt,sharing=locked \
    apt-get update && apt-get install -y \
    build-essential \
    cmake \
    git \
    gcc-aarch64-linux-gnu