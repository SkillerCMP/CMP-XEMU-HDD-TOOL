# Xemu HDD Tools 0.6b — Third-Party Notices and Provenance

This file preserves third-party/provenance information required for the distributed source and Windows helper package. It is not part of the end-user Help manual.

## QEMU / qemu-img

The Windows package obtains `qemu-img.exe` from the pinned QEMU for Windows installer used by this release. The Docker build verifies the configured installer checksum, extracts the helper, and packages `qemu-img.exe` plus its recursive non-system DLL dependency set.

`qemu-img-provenance.json` in the generated Windows `tools` directory records the exact helper source/checksum and hashes of packaged files. Available upstream license/copyright notices are copied into `tools/licenses/`.

QEMU and its dependent libraries retain their own licenses. Review the packaged notices and upstream source/redistribution obligations before redistributing a built Windows package.

No Windows system DLL, Xbox dashboard/BIOS, game, user media, or font is bundled by this project.

QEMU references:

- https://qemu.weilnetz.de/w64/
- https://www.qemu.org/download/
- https://www.qemu.org/docs/master/tools/qemu-img.html

## TEST12 compatibility provenance

The Custom Soundtrack Builder compatibility work was derived from the established TEST12 XEMU Debug Tools soundtrack implementation used as the project compatibility baseline.

Compatibility overlay SHA-256:

```text
7AB31101AC847EF84358FFE8FEAB2EC6FBEE073FC05D3C4ECAECAB592A494A9E
```

Full TEST12 builder SHA-256:

```text
9E16197BF6A221EB1BB1930393589B1776756082251CFB2A6836B819A8C8E0D9
```

The retained soundtrack format header is byte-for-byte compatible with the TEST12 baseline. The standalone C++ soundtrack core adapts file opening for UTF-8/Windows filesystem paths while retaining the established ST.DB serialization, counters/index behavior, song packing, UTF-16 field handling, WMA-profile validation, and ASF-duration handling.

The independent FATX reference implementation under `tests/reference/` is test-only and is not linked into the converter application. Existing copyright and GPL notices in those source files are retained.

The standalone application does not include the XEMU SDL/ImGui controller, live Kernel RPC writer, detached-window implementation, or TEST10 audio player.

## FFmpeg

FFmpeg is not distributed by Xemu HDD Tools. Users who want soundtrack audio conversion provide their own trusted `ffmpeg.exe`.

FFmpeg project information:

- https://ffmpeg.org/

## Project license

Xemu HDD Tools is distributed under the license in `LICENSE`. Third-party components and externally supplied helper software retain their own applicable licenses.
