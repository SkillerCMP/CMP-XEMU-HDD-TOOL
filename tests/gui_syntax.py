#!/usr/bin/env python3
"""Compile full Win32 GUI source with declaration facades; NOT a Windows SDK test."""
import argparse
import shutil
import subprocess
from pathlib import Path


def main():
    ap=argparse.ArgumentParser(description=__doc__)
    ap.add_argument('--compiler',required=True)
    args=ap.parse_args()
    compiler=shutil.which(args.compiler)
    if not compiler:
        print('SKIP: compiler unavailable');return 77
    root=Path(__file__).resolve().parents[1]
    version=subprocess.check_output([compiler,'--version'],text=True)
    if not any(s in version.lower() for s in ('gcc','g++','clang','free software foundation')):
        print('SKIP: declaration syntax harness requires GCC/Clang');return 77
    cmd=[compiler,'-std=c++17','-Wall','-Wextra','-Wpedantic','-Werror',
         '-D_WIN32','-DUNICODE','-D_UNICODE','-D_WIN32_WINNT=0x0A00',
         '-I'+str(root/'tests/win32-declarations'),'-I'+str(root/'include'),'-I'+str(root/'src'),
         '-fsyntax-only',str(root/'src/gui_csb_win32.cpp'),str(root/'src/gui_win32.cpp'),str(root/'src/gui_hdd_win32.cpp'),str(root/'src/gui_splitter_win32.cpp'),str(root/'src/gui_workspace_win32.cpp'),str(root/'src/gui_theme_win32.cpp')]
    subprocess.run(cmd,check=True)
    print('PASS: complete Converter + CSB + HDD Directory + splitter + global workspace + theme Win32 GUI syntax with typed declaration facades.')
    print('NOT a real MinGW/Windows SDK compilation, linking, mouse/DPI or GUI runtime test.')
    return 0

if __name__=='__main__':raise SystemExit(main())
