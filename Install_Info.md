# Xemu HDD Tools 0.6b — Install and Build Information

This file covers building and preparing the Windows package. Program usage belongs in `HELP.md`.

## Requirements

- Windows 10 or 11
- Docker Desktop running in **Linux-container mode**
- enough free disk space for the Docker build and HDD outputs

## Build the Windows package

From the source folder, run:

```text
Build-Windows-Docker.bat
```

The build output is placed under:

```text
dist\windows-x64\
```

The main programs are:

```text
dist\windows-x64\Xemu-HDD-Tools.exe
dist\windows-x64\xemu-hdd-convert.exe
```

Each Docker build creates a timestamped log under:

```text
logs\
```

The Docker build cross-compiles and packages the Windows applications. It does **not** perform native Windows runtime validation, execute Wine, run HDD conversions, or run the development regression suite.

## qemu-img setup

The Windows Docker build downloads the pinned QEMU for Windows package, verifies its configured checksum, and packages `qemu-img.exe` plus the DLLs it requires.

The runtime location is:

```text
tools\qemu-img.exe
```

No manual qemu-img installation is normally required when using the Docker-built Windows package.

## FFmpeg setup

FFmpeg is not bundled with Xemu HDD Tools.

If you want to import new audio through the Custom Soundtrack Builder, place a trusted Windows FFmpeg executable at:

```text
tools\ffmpeg.exe
```

The expected installed layout is therefore similar to:

```text
Xemu-HDD-Tools.exe
xemu-hdd-convert.exe
HELP.md
Install_Info.md
LICENSE
THIRD_PARTY_NOTICES.md
tools\
  qemu-img.exe
  ffmpeg.exe
  <qemu-img dependency DLLs>
  licenses\
```

FFmpeg can also be stored elsewhere and selected through:

```text
Options > Helper locations...
```

## Source layout

```text
.clang-format            C++ source-formatting policy
include/                 Shared C++ headers and interfaces
src/                     Program and backend implementation
tests/                   Automated regression/integration tests
tests/support/           Test-only developer helpers
tools/                   Build packaging helper (`package_qemu.py`)
cmake/                   CMake toolchain support
HELP.md                  Program capabilities and usage
Install_Info.md          Build/install/setup information
VALIDATION.md            Validation record for this source release
THIRD_PARTY_NOTICES.md   Third-party/provenance notices
LICENSE                  Project license
```

`include/` and `src/` are intentionally kept side-by-side. `.clang-format` keeps the C++ source style consistent without reordering includes or `using` declarations. `tools/` in the source tree is for the QEMU packaging helper; the Windows build output also has a `tools/` directory containing runtime helper executables.

## Developer validation

Normal developer validation uses CMake/CTest:

```sh
cmake -S . -B build-gcc -G Ninja -DCMAKE_BUILD_TYPE=Release
cmake --build build-gcc --parallel 2
ctest --test-dir build-gcc --output-on-failure
```

Clang with sanitizers:

```sh
cmake -S . -B build-clang -G Ninja \
  -DCMAKE_BUILD_TYPE=RelWithDebInfo \
  -DCMAKE_CXX_COMPILER=clang++ \
  -DXHC_SANITIZERS=ON
cmake --build build-clang --parallel 2
ASAN_OPTIONS=detect_leaks=1:halt_on_error=1 \
UBSAN_OPTIONS=halt_on_error=1 \
ctest --test-dir build-clang --output-on-failure
```

Real qemu-img integration can be made mandatory in a development environment with:

```text
-DXHC_REQUIRE_QEMU_TESTS=ON
```

See `VALIDATION.md` for the validation status of the packaged source release.
