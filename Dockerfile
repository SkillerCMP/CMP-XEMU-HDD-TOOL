# syntax=docker/dockerfile:1
# Docker Desktop (Linux containers) cross-compiles the Windows x64 executables.
# A separate packaging stage downloads a pinned Windows QEMU installer, verifies
# its SHA-512, and copies qemu-img.exe plus its non-system DLL closure. Nothing
# from QEMU is executed during the Docker build.

ARG QEMU_WIN_URL=https://qemu.weilnetz.de/w64/qemu-w64-setup-20260811.exe
ARG QEMU_WIN_SHA512=5bcf9eed634e8575a37b74f445af41a2fe4106da512d0c30c368301d4c105037fdfab40a5287367a28a957624cddebbc8c07e16c88ab6634f554cdf3d16bf543

FROM ubuntu:24.04 AS qemu-helper
ARG QEMU_WIN_URL
ARG QEMU_WIN_SHA512
ENV DEBIAN_FRONTEND=noninteractive
RUN apt-get update && apt-get install -y --no-install-recommends \
    ca-certificates curl python3 p7zip-full binutils-mingw-w64-x86-64 \
    && rm -rf /var/lib/apt/lists/*
WORKDIR /src
COPY tools/package_qemu.py /src/tools/package_qemu.py
RUN curl --fail --location --retry 3 --proto '=https' --tlsv1.2 "$QEMU_WIN_URL" -o /tmp/qemu-installer.exe \
    && printf '%s  %s\n' "$QEMU_WIN_SHA512" /tmp/qemu-installer.exe | sha512sum -c - \
    && mkdir -p /tmp/qemu-extracted /qemu-tools \
    && 7z x -y -o/tmp/qemu-extracted /tmp/qemu-installer.exe > /tmp/qemu-extraction.log \
    && python3 /src/tools/package_qemu.py --extracted /tmp/qemu-extracted --output /qemu-tools \
       --installer-sha512 "$QEMU_WIN_SHA512" --url "$QEMU_WIN_URL" \
    && rm -rf /tmp/qemu-installer.exe /tmp/qemu-extracted

FROM ubuntu:24.04 AS windows-build
ENV DEBIAN_FRONTEND=noninteractive
RUN apt-get update && apt-get install -y --no-install-recommends \
    ca-certificates cmake ninja-build \
    g++-mingw-w64-x86-64-posix binutils-mingw-w64-x86-64 \
    && rm -rf /var/lib/apt/lists/*
WORKDIR /src
COPY . .
# ZIP extraction can preserve timestamps newer than the container clock. Normalize only
# Docker's disposable source copy so Ninja/CMake cannot enter a regeneration loop.
RUN find /src -type f -exec touch -d '2000-01-01 00:00:00 UTC' {} + \
    && cmake -S . -B /build/windows -G Ninja -DCMAKE_BUILD_TYPE=Release \
       -DCMAKE_TOOLCHAIN_FILE=cmake/mingw64.cmake -DXHC_BUILD_TESTS=OFF \
    && cmake --build /build/windows --parallel 2 \
    && mkdir -p /out/windows-x64/tools \
    && cp /build/windows/Xemu-HDD-Tools.exe /build/windows/xemu-hdd-convert.exe /out/windows-x64/ \
    && cp HELP.md Install_Info.md THIRD_PARTY_NOTICES.md VALIDATION.md LICENSE BUILD-REVISION.txt /out/windows-x64/
COPY --from=qemu-helper /qemu-tools/ /out/windows-x64/tools/

FROM scratch AS windows-artifacts
COPY --from=windows-build /out/ /

FROM windows-artifacts AS default
