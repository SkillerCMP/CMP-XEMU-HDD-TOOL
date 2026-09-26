#!/usr/bin/env python3
"""Exercise the real preflight, including a controlled missing compiler-rt case.

The compiler's resource headers are borrowed read-only. Only a disposable resource
folder lacks runtimes; no installed compiler or runtime file is altered.
"""
import argparse
import hashlib
from pathlib import Path
import subprocess
import sys
import tempfile

ROOT = Path(__file__).resolve().parent.parent
COUNT = 0


def check(ok: bool, message: str) -> None:
    global COUNT
    if not ok:
        raise RuntimeError(message)
    COUNT += 1
    print('PASS:', message, flush=True)


def invoke(compiler: str, extra: list[str] | None = None) -> subprocess.CompletedProcess[str]:
    command = [sys.executable, str(ROOT / 'tests/support/check_sanitizers.py'), '--compiler', compiler]
    result = subprocess.run(command + (extra or []), stdout=subprocess.PIPE,
                            stderr=subprocess.STDOUT, text=True, timeout=180, check=False)
    print(result.stdout, flush=True)
    return result


def runtime_hashes(resource: Path) -> dict[str, str]:
    result = {}
    for path in sorted((resource / 'lib').rglob('libclang_rt.asan*.a')):
        if path.is_file():
            with path.open('rb') as stream:
                result[str(path)] = hashlib.file_digest(stream, 'sha256').hexdigest()
    return result


def main() -> int:
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument('--compiler', required=True)
    args = parser.parse_args()
    info = subprocess.run([args.compiler, '-print-resource-dir'], stdout=subprocess.PIPE,
                          stderr=subprocess.PIPE, text=True, check=True, timeout=30)
    resource = Path(info.stdout.strip()).resolve(strict=True)
    check((resource / 'include').is_dir(), 'real compiler resource headers exist')
    before = runtime_hashes(resource)
    check(bool(before), 'real compiler-rt archives exist for toolchain test')
    with tempfile.TemporaryDirectory(prefix='xhc-runtime-regression-') as temp:
        missing = Path(temp) / 'missing-runtimes'
        missing.mkdir()
        (missing / 'include').symlink_to(resource / 'include', target_is_directory=True)
        if (resource / 'share').is_dir():
            (missing / 'share').symlink_to(resource / 'share', target_is_directory=True)
        failure = invoke(args.compiler, ['--compiler-arg=-resource-dir=' + str(missing)])
        check(failure.returncode == 1, 'missing runtime fails preflight')
        check('libclang_rt.asan' in failure.stdout, 'real linker diagnoses missing ASan runtime')
        check('ASan/UBSan C++ compile/link failed' in failure.stdout,
              'failure explains matching compiler-rt dependency')
        check('PASS: Clang ASan/UBSan' not in failure.stdout, 'failure is never claimed as success')
        absent = invoke(str(Path(temp) / 'no-such-compiler'))
        check(absent.returncode == 1 and 'preflight FAILED' in absent.stdout,
              'missing compiler reports failure without passing')
    healthy = invoke(args.compiler)
    check(healthy.returncode == 0 and 'PASS: Clang ASan/UBSan' in healthy.stdout,
          'installed runtime passes compile/link and clean/ASan/UBSan probes')
    check('EXPECTED DIAGNOSTIC' in healthy.stdout, 'deliberate detector faults are explicitly labelled')
    check(runtime_hashes(resource) == before, 'installed compiler-rt archives remain byte-identical')
    print('PASS:', COUNT, 'sanitizer-toolchain checks using the real native Clang/linker/runtime.')
    return 0


if __name__ == '__main__':
    try:
        sys.exit(main())
    except (OSError, RuntimeError, subprocess.SubprocessError) as exc:
        print('FAIL:', exc, file=sys.stderr)
        sys.exit(1)
