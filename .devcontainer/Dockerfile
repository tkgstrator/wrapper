FROM mcr.microsoft.com/devcontainers/cpp:2.1.2-bookworm

ARG NDK_VERSION=23
ENV ANDROID_NDK_ROOT=/opt/android-ndk-r${NDK_VERSION}b

RUN curl -fsSL -o /tmp/ndk.zip \
        https://dl.google.com/android/repository/android-ndk-r${NDK_VERSION}b-linux.zip \
    && unzip -q /tmp/ndk.zip -d /opt \
    && rm /tmp/ndk.zip
