#!/usr/bin/env python3
"""Normalize only a disposable source copy before its first CMake configure.

ZIP-extracted inputs can be newer than the container clock. A fixed past epoch
keeps cached Docker source layers safe even if the build clock later moves back.
This helper never changes file contents and is not a disk-conversion operation.
"""
import argparse
from datetime import datetime, timezone
import os
from pathlib import Path
import stat
import sys
import time

SOURCE_EPOCH_NS = 946684800 * 1_000_000_000  # 2000-01-01 00:00:00 UTC
EXCLUDED_DIRS = {'.git', '__pycache__'}


def normalize(root: Path) -> None:
    if root.is_symlink():
        raise ValueError('Source root must not be a symbolic link.')
    root = root.resolve(strict=True)
    if root == root.parent or not (root / 'CMakeLists.txt').is_file():
        raise ValueError('Expected a copied CMake source tree, not a filesystem root.')
    now_ns = time.time_ns()
    if now_ns <= SOURCE_EPOCH_NS:
        raise ValueError('Build clock is not after 2000-01-01; correct the build clock first.')
    files = []
    # Validate the complete walk before touching any timestamps. Never follow links.
    for current, dirs, names in os.walk(root, followlinks=False):
        dirs[:] = sorted(d for d in dirs if d not in EXCLUDED_DIRS)
        for name in dirs + sorted(names):
            path = Path(current) / name
            info = path.lstat()
            if stat.S_ISLNK(info.st_mode):
                raise ValueError('Symbolic link in source copy: ' + str(path.relative_to(root)))
            if stat.S_ISDIR(info.st_mode):
                continue
            if not stat.S_ISREG(info.st_mode):
                raise ValueError('Non-regular source entry: ' + str(path.relative_to(root)))
            if path.name in {'CMakeCache.txt', 'build.ninja'}:
                raise ValueError('Generated build files in source copy; use a clean, out-of-source build: ' + str(path))
            files.append((path, info))
    print('Build-source timestamp normalization (file contents unchanged)')
    print('Source copy:', root)
    print('Build clock UTC:', datetime.fromtimestamp(now_ns / 1e9, timezone.utc).isoformat())
    print('Normalized epoch UTC: 2000-01-01T00:00:00+00:00')
    future = [(p, s) for p, s in files if s.st_mtime_ns > now_ns]
    print('Future-dated files observed:', len(future))
    for path, info in future:
        stamp = datetime.fromtimestamp(info.st_mtime_ns / 1e9, timezone.utc).isoformat()
        print('  Future input:', path.relative_to(root), stamp)
    for path, info in files:
        os.utime(path, ns=(info.st_atime_ns, SOURCE_EPOCH_NS), follow_symlinks=False)
        if path.lstat().st_mtime_ns != SOURCE_EPOCH_NS:
            raise OSError('Failed to normalize timestamp: ' + str(path))
    print('Normalized regular files:', len(files))
    print('CMake regeneration and all verification gates remain enabled.')


def main() -> int:
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument('--source-root', required=True, type=Path)
    args = parser.parse_args()
    try:
        normalize(args.source_root)
    except (OSError, ValueError) as exc:
        print('Build-source preparation failed:', exc, file=sys.stderr)
        return 1
    return 0


if __name__ == '__main__':
    sys.exit(main())
