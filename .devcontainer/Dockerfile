# N_m3u8DL-RE
FROM dhi.io/dotnet:10-sdk AS build-dotnet

ARG TARGETARCH

RUN \
    --mount=type=cache,target=/var/lib/apt,sharing=locked \
    --mount=type=cache,target=/var/cache/apt,sharing=locked \
    apt-get update && apt-get install -y --no-install-recommends \
    git clang

WORKDIR /
RUN git clone https://github.com/nilaoda/N_m3u8DL-RE.git --depth 1
WORKDIR /N_m3u8DL-RE
# Fix C# 14 breaking change: 'this' not allowed in nameof() in attributes
RUN sed -i 's/nameof(this\.\([a-zA-Z]*\))/nameof(\1)/g' \
    src/N_m3u8DL-RE.Parser/StreamExtractor.cs
RUN \
    --mount=type=cache,target=/root/.nuget/packages \
    case "${TARGETARCH}" in \
    arm64) RID="linux-arm64" ;; \
    amd64) RID="linux-x64"  ;; \
    *)     echo "Unsupported arch: ${TARGETARCH}" && exit 1 ;; \
    esac && \
    dotnet publish src/N_m3u8DL-RE \
    -r "${RID}" \
    -c Release \
    -p:StripSymbols=true \
    -p:CppCompilerAndLinker=clang \
    -o /usr/local/bin

# FROM mcr.microsoft.com/devcontainers/python:3.10-bookworm
FROM mcr.microsoft.com/devcontainers/base:dev-ubuntu22.04

# gamdl 3.6+ does native muxing/decryption, so mp4decrypt/MP4Box/amdecrypt are no longer needed.
# ffmpeg (for the N_m3u8DL-RE download mode) is provided via the ffmpeg-apt-get devcontainer feature.
COPY --from=build-dotnet /usr/local/bin/N_m3u8DL-RE /usr/local/bin/
