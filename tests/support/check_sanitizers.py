#!/usr/bin/env python3
"""Compile, link, and exercise a disposable native Clang ASan/UBSan probe.

Runs no converter code and touches no disk images. Expected sanitizer failures
occur only in the deliberately faulty probe modes; their diagnostics are checked.
"""
import argparse
import os
from pathlib import Path
import shlex
import subprocess
import sys
import tempfile

PROBE = r'''
#include <limits>
#include <memory>
#include <string>
#include <vector>

int main(int argc, char** argv) {
    if (argc != 2) return 2;
    const std::string mode(argv[1]);
    if (mode == "clean") {
        const std::vector<int> values{1, 2, 3, 4};
        auto copy = std::make_unique<int[]>(values.size());
        int sum = 0;
        for (std::size_t i = 0; i < values.size(); ++i) {
            copy[i] = values.at(i);
            sum += copy[i];
        }
        return sum == 10 ? 0 : 3;
    }
    if (mode == "asan") {
        auto data = std::make_unique<int[]>(1);
        volatile int index = 3;
        data[index] = 42; // Deliberate heap overflow in a test-only process.
        return data[0];
    }
    if (mode == "ubsan") {
        volatile int maximum = std::numeric_limits<int>::max();
        return maximum + argc; // Deliberate signed overflow in a test-only process.
    }
    return 4;
}
'''


def run(command: list[str], env: dict[str, str] | None = None) -> subprocess.CompletedProcess[str]:
    print('+', shlex.join(command), flush=True)
    result = subprocess.run(command, env=env, text=True, stdout=subprocess.PIPE,
                            stderr=subprocess.STDOUT, timeout=120, check=False)
    if result.stdout:
        print(result.stdout, end='' if result.stdout.endswith('\n') else '\n', flush=True)
    return result


def check(compiler: str, extra_args: list[str]) -> None:
    version = run([compiler, '--version'])
    if version.returncode != 0 or 'clang' not in version.stdout.lower():
        raise RuntimeError('Expected a usable native Clang C++ compiler.')
    resource = run([compiler, *extra_args, '-print-resource-dir'])
    if resource.returncode != 0:
        raise RuntimeError('Clang resource-directory query failed.')
    # Append our required settings last so inherited options cannot mask failures.
    env = os.environ.copy()
    for name, required in (
        ('ASAN_OPTIONS', 'detect_leaks=1:halt_on_error=1:abort_on_error=0:disable_coredump=1'),
        ('UBSAN_OPTIONS', 'halt_on_error=1:print_stacktrace=1'),
    ):
        old = env.get(name, '')
        env[name] = (old + ':' if old else '') + required
    with tempfile.TemporaryDirectory(prefix='xhc-sanitizer-check-') as temp:
        root = Path(temp)
        source = root / 'probe.cpp'
        binary = root / 'probe'
        source.write_text(PROBE, encoding='utf-8')
        build = run([compiler, *extra_args, '-std=c++17', '-O0', '-g', '-Wall', '-Wextra',
                     '-Wpedantic', '-Werror', '-fno-omit-frame-pointer',
                     '-fsanitize=address,undefined', str(source), '-o', str(binary)])
        if build.returncode != 0:
            raise RuntimeError(
                'ASan/UBSan C++ compile/link failed. Install the compiler-rt package '
                'matching the selected Clang major version. Docker Ubuntu 24.04 uses '
                'clang-18 plus libclang-rt-18-dev; sanitizer checks must not be disabled.')
        if run([str(binary), 'clean'], env).returncode != 0:
            raise RuntimeError('Valid C++ ASan/UBSan probe did not run cleanly.')
        for mode, marker in (
            ('asan', 'AddressSanitizer: heap-buffer-overflow'),
            ('ubsan', 'runtime error: signed integer overflow'),
        ):
            print('EXPECTED DIAGNOSTIC: deliberate ' + mode.upper() + ' test-only fault', flush=True)
            failure = run([str(binary), mode], env)
            if failure.returncode == 0 or marker not in failure.stdout:
                raise RuntimeError(mode.upper() + ' probe failed to detect the expected fault.')
    print('PASS: Clang ASan/UBSan compile, link, clean run, heap-overflow detection, '
          'and signed-overflow detection.', flush=True)


def main() -> int:
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument('--compiler', required=True,
                        help='Native Clang C++ executable; Docker selects clang++-18.')
    parser.add_argument('--compiler-arg', action='append', default=[],
                        help='Extra driver argument, used by missing-runtime regression fixtures.')
    args = parser.parse_args()
    try:
        check(args.compiler, args.compiler_arg)
    except (OSError, RuntimeError, subprocess.TimeoutExpired) as exc:
        print('Sanitizer toolchain preflight FAILED:', exc, file=sys.stderr, flush=True)
        return 1
    return 0


if __name__ == '__main__':
    sys.exit(main())
