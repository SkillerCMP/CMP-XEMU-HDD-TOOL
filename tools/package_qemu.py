#!/usr/bin/env python3
"""Collect the pinned upstream Windows qemu-img and its non-system DLL closure.
Run only inside the build container after installer SHA-512 verification.
No program is executed while collecting files.
"""
import argparse
import hashlib
import json
from pathlib import Path
import re
import shutil
import subprocess

SYSTEM = set('kernel32 ntdll advapi32 msvcrt ucrtbase rpcrt4 user32 gdi32 ws2_32 crypt32 secur32 shell32 shlwapi ole32 oleaut32 comdlg32 comctl32 version winmm imm32 normaliz userenv bcrypt ncrypt psapi dbghelp setupapi cfgmgr32 iphlpapi netapi32 dnsapi wsock32 powrprof wintrust wldap32 imagehlp dwmapi usp10 combase propsys shcore winhttp wininet authz wtsapi32 avrt msimg32 mpr cabinet sechost hid oleacc dxgi d3d11 d3d12 opengl32 glu32 win32u'.split())
SYSTEM.update(('winspool.drv','kernelbase','cryptbase'))


def sha(path):
    with path.open('rb') as f:
        return hashlib.file_digest(f,'sha256').hexdigest().upper()


def main():
    parser=argparse.ArgumentParser()
    parser.add_argument('--extracted',type=Path,required=True)
    parser.add_argument('--output',type=Path,required=True)
    parser.add_argument('--installer-sha512',required=True)
    parser.add_argument('--url',required=True)
    args=parser.parse_args()
    lookup={}
    for p in args.extracted.rglob('*'):
        if p.is_file():lookup.setdefault(p.name.lower(),[]).append(p)
    def resolve(name):
        matches=lookup.get(name.lower(),[])
        if not matches:raise RuntimeError('Missing qemu-img dependency: '+name)
        hashes={sha(p) for p in matches}
        if len(hashes)>1:raise RuntimeError('Ambiguous different dependency copies: '+name)
        return sorted(matches,key=lambda p:(len(p.parts),str(p)))[0]
    output=args.output
    output.mkdir(parents=True,exist_ok=True)
    pending=[resolve('qemu-img.exe')];copied={}
    while pending:
        source=pending.pop()
        name=source.name.lower()
        if name in copied:continue
        shutil.copy2(source,output/source.name)
        copied[name]={'file':source.name,'sha256':sha(source),'upstream_path':str(source.relative_to(args.extracted))}
        info=subprocess.run(['x86_64-w64-mingw32-objdump','-p',str(source)],check=True,text=True,stdout=subprocess.PIPE).stdout
        for dll in re.findall(r'DLL Name:\s*(\S+)',info):
            lower=dll.lower()
            if lower.startswith(('api-ms-win-','ext-ms-win-')) or lower.removesuffix('.dll') in SYSTEM or lower in SYSTEM:
                continue
            pending.append(resolve(dll))
    licenses=output/'licenses';licenses.mkdir(exist_ok=True)
    for p in args.extracted.rglob('*'):
        if p.is_file() and any(word in p.name.lower() for word in ('license','licence','copying','copyright')):
            rel=p.relative_to(args.extracted)
            destination=licenses/rel
            destination.parent.mkdir(parents=True,exist_ok=True)
            shutil.copy2(p,destination)
    metadata={'source_url':args.url,'installer_sha512':args.installer_sha512,
              'packaging':'qemu-img plus recursive PE import dependencies; system DLLs are not copied',
              'files':list(copied.values())}
    (output/'qemu-img-provenance.json').write_text(json.dumps(metadata,indent=2)+'\n')
    print('Packaged',len(copied),'qemu-img files; preserved available upstream license notices.')

if __name__=='__main__':main()
