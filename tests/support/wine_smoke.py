#!/usr/bin/env python3
"""Execute real Windows CLI + packaged Windows qemu-img under Wine.
This is a build-gate, not a claim of native Windows GUI/dashboard execution.
"""
import argparse
import hashlib
import json
import os
from pathlib import Path
import subprocess
import sys
import tempfile
sys.path.insert(0,str(Path(__file__).resolve().parent.parent))
from integration import make_fixture, inventory, scan_allocated_for_marker, BLOAT


def win(path):
    return 'Z:'+str(Path(path).resolve()).replace('/','\\')


def main():
    ap=argparse.ArgumentParser();ap.add_argument('--wine',default='/usr/lib/wine/wine64')
    ap.add_argument('--package',type=Path,required=True);ap.add_argument('--unit',type=Path,required=True)
    args=ap.parse_args();env=os.environ.copy();env.update(WINEDEBUG='-all',WINEDLLOVERRIDES='mscoree,mshtml=d',WINEARCH='win64')
    def run(binary,*parameters,success=True):
        p=subprocess.run([args.wine,str(binary),*parameters],stdout=subprocess.PIPE,stderr=subprocess.PIPE,text=True,env=env,timeout=600)
        if (p.returncode==0)!=success:raise RuntimeError('Wine Windows command failed:\n'+p.stdout+'\n'+p.stderr)
        return p
    exe=args.package/'xemu-hdd-convert.exe';helper=args.package/'tools/qemu-img.exe'
    print(run(exe,'--version').stdout,flush=True)
    print(run(args.unit).stdout,flush=True)
    print(run(helper,'--version').stdout,flush=True)
    with tempfile.TemporaryDirectory(prefix='xhc-wine-') as tmp:
        root=Path(tmp);raw=root/'Windows source with spaces.raw';expected,_=make_fixture(raw)
        qcow=root/'source.qcow2'
        run(helper,'convert','-f','raw','-O','qcow2',win(raw),win(qcow))
        before=hashlib.sha256(qcow.read_bytes()).hexdigest()
        folder=root/'Windows Folder-HDD'
        run(exe,'image-to-folder','--source',win(qcow),'--output',win(folder),'--offline')
        assert inventory(folder/'Converter-Metadata/conversion-report.json')==expected
        result=root/'Windows output.qcow2'
        run(exe,'folder-to-image','--source',win(folder),'--output',win(result),'--offline')
        assert inventory(str(result)+'.conversion.json')==expected
        decoded=root/'Windows decoded.raw'
        run(helper,'convert','-f','qcow2','-O','raw',win(result),win(decoded));scan_allocated_for_marker(decoded,BLOAT)
        assert hashlib.sha256(qcow.read_bytes()).hexdigest()==before
        run(exe,'image-to-folder','--source',win(qcow),'--output',win(folder),'--offline',success=False)
        # Opening for write through a separate Windows process is not bypassed.
        print('PASS: Windows CLI and packaged Windows qemu-img under Wine: both directions, source hash, clean slack/free space, existing destination refusal.',flush=True)

if __name__=='__main__':main()
