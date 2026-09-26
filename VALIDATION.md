# Xemu HDD Tools 0.6b — Validation

This is the validation record for the current Xemu HDD Tools 0.6b source release.

## Release structure

```text
.clang-format            C++ source-formatting policy
include/                 Shared C++ headers and interfaces
src/                     Application and backend implementation
tests/                   Regression and integration tests
tests/support/           Test-only developer helpers
tools/                   Product/build packaging helper
cmake/                   CMake toolchain support
HELP.md                  Program capabilities and usage
Install_Info.md          Build/install/setup information
THIRD_PARTY_NOTICES.md   Third-party/provenance notices
VALIDATION.md            Current-release validation record
LICENSE                  Project license
```

The source `tools/` directory contains only `package_qemu.py`, used by the Windows Docker build to package `qemu-img.exe` and its required DLLs. Test-only helpers remain under `tests/support/`.

There is no `third_party/` folder in the release. Required third-party/provenance information is consolidated into `THIRD_PARTY_NOTICES.md`.

## Documentation boundary

The release documentation is intentionally separated by purpose:

- `HELP.md` contains only information about the current program, its capabilities, GUI/CLI usage, supported HDD formats, safety/recovery behavior, and the runtime helper files `tools\qemu-img.exe` and `tools\ffmpeg.exe`.
- `Install_Info.md` contains Windows Docker build/install/setup information and developer validation commands.
- `THIRD_PARTY_NOTICES.md` contains third-party licensing/provenance information.
- `VALIDATION.md` records validation of the current source release.

## Source-package integrity

The release uses `source-sha256.json` to cover every packaged source/support file except the manifest itself. The packaged release contains **89 manifest-listed files**. Final fresh-extraction verification requires all 89 hashes to match with no unexpected source files.

## Source hygiene

The C++ implementation and public headers use a checked-in `.clang-format` policy with include and `using` declaration order preserved. The cleanup intentionally changes presentation only: spacing, indentation, line wrapping, and a small number of explanatory comments.

A lexical equivalence check compared all **31 C/C++ files** under `src/` and `include/` against the functional source before formatting. After removing whitespace and comments, **31/31 matched exactly**. No operators, constants, strings, declarations, calls, conditions, or function bodies changed. The formatting pass did not change `app.rc` or `app.manifest`. The application icon is maintained separately as a resource asset and is shared by both `Xemu-HDD-Tools.exe` and `xemu-hdd-convert.exe` through `src/app.rc`.

The static hygiene checks also verify that:

- source files contain no `TODO`, `FIXME`, `HACK`, or `XXX` scratch markers;
- no oversized line-comment block is present;
- the formatting policy does not reorder includes or `using` declarations;
- dependency-sensitive include order remains unchanged.

## Application icon integrity

The current `src/xemu-hdd-tools.ico` is the transparent-edge application resource used by both Windows executables. It contains explicit **16, 20, 24, 32, 40, 48, 64, 96, 128, and 256 pixel** frames so Explorer does not have to reuse an older presentation frame at intermediate sizes.

During asset construction, every embedded frame was checked to have a fully transparent outer edge. The packaging suite also verifies the complete size table and the exact known-good icon resource hash. `assets/xemu-hdd-tools-icon.png` is the matching transparent source artwork.

## GCC Release validation

The project is validated with CMake/Ninja in Release mode with strict compiler warnings enabled.

Current test group:

```text
ctest --test-dir build-gcc \
  -E 'csb-integration|hdd-integration|clean-conversions' \
  --output-on-failure
```

Current result for this package:

```text
13/13 current tests passed
0 test failures
```

Covered tests:

- theme-unit
- workspace-unit
- unit
- csb-unit
- hdd-unit
- gui-theme
- gui-shell
- gui-workspace
- gui-controls
- gui-declaration-syntax
- packaging
- build-timestamps
- transaction-faults

## Clang ASan/UBSan validation

The sanitizer configuration adds `sanitizer-toolchain` to the current group.

Current result:

```text
14/14 current tests passed
0 test failures
```

## Packaging validation

The current static packaging suite was executed against this source tree and reports:

```text
PASS: 353 Xemu HDD Tools v0.6b current-release packaging checks
```

The checks include:

- required source, icon, build, Help, install, validation, license, notice, and formatting-policy files are present;
- the embedded ICO contains the complete Windows size set and matches the verified transparent-edge resource;
- source-hygiene guards reject scratch markers and oversized comment blocks;
- the mixed historical README is absent;
- the legacy `third_party/`, `docs/`, and historical `validation/` directories are absent;
- `HELP.md` contains program/usage/helper information but not Docker build instructions;
- `Install_Info.md` contains the Windows Docker build and helper setup instructions;
- `THIRD_PARTY_NOTICES.md` preserves QEMU, TEST12 compatibility, and FFmpeg provenance/notices;
- `tools/` contains only `package_qemu.py`;
- test-only helper scripts remain under `tests/support/`;
- the Windows Docker build remains compile/package-only;
- the generated Windows package includes `HELP.md`, `Install_Info.md`, `THIRD_PARTY_NOTICES.md`, `VALIDATION.md`, and `LICENSE`;
- GUI, CSB, HDD Directory, workspace, recovery, source-protection, theme, icon, and version invariants remain present.

## Long integration tests

The configured suite also contains three long-running synthetic-disk tests:

- `csb-integration`
- `hdd-integration`
- `clean-conversions`

They remain part of the normal CTest configuration and can be run in an unrestricted environment with:

```text
ctest --test-dir build-gcc --output-on-failure
```

## Windows and Docker acceptance boundary

Authoring-side validation does not replace native Windows runtime acceptance. A Windows acceptance run should verify the three GUI tabs, themes, helper discovery, file pickers/context menus, embedded icon, CLI `--help`/`--version`, and a disposable or backed-up HDD workflow.
