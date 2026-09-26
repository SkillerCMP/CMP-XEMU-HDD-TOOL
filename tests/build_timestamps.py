#!/usr/bin/env python3
"""Real CMake/Ninja clock-skew regression; no Docker or Windows execution implied."""
import argparse
import hashlib
import os
from pathlib import Path
import shutil
import subprocess
import sys
import tempfile
import time

parser = argparse.ArgumentParser(description=__doc__)
parser.add_argument('--cmake', default='cmake')
parser.add_argument('--compiler', default='c++')
args = parser.parse_args()
root = Path(__file__).resolve().parent.parent
normalizer = root / 'tests/support/normalize_build_timestamps.py'
epoch_ns = 946684800 * 1_000_000_000
checks = 0


def check(ok, message):
    global checks
    if not ok:
        raise AssertionError(message)
    checks += 1


def run(command, success=True):
    result = subprocess.run([str(a) for a in command], stdout=subprocess.PIPE,
                            stderr=subprocess.STDOUT, text=True, timeout=90)
    check((result.returncode == 0) == success,
          'Command status: ' + repr(command) + '\n' + result.stdout)
    return result.stdout


def signature(path):
    return hashlib.sha256(path.read_bytes()).hexdigest(), path.stat().st_mode


if not shutil.which('ninja'):
    # Optional for native Makefile users only. Docker always provides Ninja.
    print('SKIP: CMake/Ninja timestamp regression requires ninja.')
    sys.exit(77)

with tempfile.TemporaryDirectory(prefix='xhc clock skew ') as tmp:
    work = Path(tmp)
    source = work / 'source copy'
    source.mkdir()
    (source / 'cmake').mkdir()
    (source / 'CMakeLists.txt').write_text('''cmake_minimum_required(VERSION 3.20)
project(TimestampProbe LANGUAGES CXX)
include(cmake/options.cmake)
configure_file(probe.hpp.in generated/probe.hpp @ONLY)
add_executable(probe main.cpp)
target_include_directories(probe PRIVATE "${CMAKE_CURRENT_BINARY_DIR}/generated")
''')
    options = source / 'cmake/options.cmake'
    options.write_text('set(PROBE_VALUE 7)\n')
    (source / 'cmake/probe-toolchain.cmake').write_text('# Native test toolchain input.\n')
    (source / 'probe.hpp.in').write_text('#define PROBE_VALUE @PROBE_VALUE@\n')
    (source / 'main.cpp').write_text('#include "probe.hpp"\n#include <iostream>\nint main(){std::cout << PROBE_VALUE << "\\n";}\n')
    paths = sorted(p for p in source.rglob('*') if p.is_file())
    original = {p: signature(p) for p in paths}
    future_ns = time.time_ns() + 2 * 86400 * 1_000_000_000
    for p in paths:
        os.utime(p, ns=(future_ns, future_ns))
    build = work / 'build first'
    def configure(destination):
        return run([args.cmake, '-S', source, '-B', destination, '-G', 'Ninja',
                    '-DCMAKE_CXX_COMPILER=' + args.compiler,
                    '-DCMAKE_TOOLCHAIN_FILE=' + str(source / 'cmake/probe-toolchain.cmake')])
    def compile_at(destination, success=True):
        return run([args.cmake, '--build', destination, '--parallel', '2', '--', '-d', 'explain'], success)
    configure(build)
    failure = compile_at(build, success=False)
    check("manifest 'build.ninja' still dirty after 100 tries" in failure, failure)
    check('older than most recent input' in failure, failure)
    check(not (build / 'probe').exists(), 'Clock-skew negative case must stop before compilation.')
    print('PASS: future-dated inputs reproduce the exact 100-regeneration failure.')
    result = run([sys.executable, normalizer, '--source-root', source])
    check(('Future-dated files observed: ' + str(len(paths))) in result, result)
    for p in paths:
        check(signature(p) == original[p], 'Normalizer changed bytes or mode: ' + str(p))
        check(p.stat().st_mtime_ns == epoch_ns, 'Incorrect normalized mtime: ' + str(p))
    print('PASS: source contents/modes preserved; all copied inputs normalized.')
    output = compile_at(build)
    check('still dirty' not in output, output)
    exe = build / ('probe.exe' if os.name == 'nt' else 'probe')
    check(run([exe]).strip() == '7', 'Wrong probe value.')
    idle = compile_at(build)
    check('no work to do' in idle and 'Re-running CMake' not in idle, idle)
    print('PASS: normalized timestamp rebuild succeeds; immediate rebuild is a no-op.')
    # Legitimate edits must continue to regenerate; do not suppress CMake's safety.
    time.sleep(1.05)
    options.write_text('set(PROBE_VALUE 9)\n')
    changed = compile_at(build)
    check('Re-running CMake' in changed, changed)
    check(run([exe]).strip() == '9', 'Real source edit was not reflected in the rebuilt program.')
    print('PASS: real CMake/module edits still regenerate and update the program.')
    second = work / 'build second'
    configure(second)
    compile_at(second)
    second_exe = second / ('probe.exe' if os.name == 'nt' else 'probe')
    check(run([second_exe]).strip() == '9', 'Shared normalized source failed in a second build directory.')
    print('PASS: a second out-of-source build uses the same prepared source.')
    # Refuse ambiguous source roots/links instead of modifying anything outside them.
    outside = work / 'outside.txt'
    outside.write_text('OUTSIDE SOURCE - DO NOT TOUCH')
    outside_stamp = outside.stat().st_mtime_ns
    before_link_check = {p: p.stat().st_mtime_ns for p in paths}
    link = source / 'outside-link.txt'
    link.symlink_to(outside)
    run([sys.executable, normalizer, '--source-root', source], success=False)
    check(outside.stat().st_mtime_ns == outside_stamp, 'Followed an external symlink.')
    check({p: p.stat().st_mtime_ns for p in paths} == before_link_check,
          'Invalid source walk must be refused before any normalization.')
    link.unlink()
    (source / 'CMakeCache.txt').write_text('TEST GENERATED CACHE')
    run([sys.executable, normalizer, '--source-root', source], success=False)
    (source / 'CMakeCache.txt').unlink()
    run([sys.executable, normalizer, '--source-root', work], success=False)
    print('PASS: external links, generated caches, and non-source roots refused.')

print('PASS:', checks, 'real CMake/Ninja build-timestamp checks.')
