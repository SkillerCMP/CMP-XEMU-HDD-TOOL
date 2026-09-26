#!/usr/bin/env python3
"""Offline HDD Directory content/transaction regressions. Not a user Docker gate."""
from pathlib import Path
import argparse, hashlib, json, os, shutil, tempfile
from integration import make_fixture, run_cmd, inventory, inspect_oracle, scan_allocated_for_marker, BLOAT
from csb_integration import raw_files

def main():
    ap=argparse.ArgumentParser();ap.add_argument('--driver',required=True);ap.add_argument('--oracle',required=True);a=ap.parse_args();count=0
    def passed(s):
        nonlocal count
        count+=1;print('PASS',count,s,flush=True)
    with tempfile.TemporaryDirectory(prefix='xhc-hdd-browser-') as temp:
        root=Path(temp);source=root/'source image.raw';make_fixture(source);original=raw_files(source)
        with source.open('rb') as f:original_hash=hashlib.file_digest(f,'sha256').hexdigest()
        imports=root/'host import';imports.mkdir();(imports/'new.bin').write_bytes(b'NEW_HOST_CONTENT');(imports/'empty').mkdir();(imports/'zero-length.bin').touch()
        (root/'Backups'/'old-recovery').mkdir(parents=True);(root/'Backups'/'old-recovery'/'keep').write_bytes(b'KEEP_OLD_BACKUP')
        def cmd(src,op,out=None,host=None,success=True):
            r=run_cmd([a.driver,src,op]+([out] if out else [])+([host] if host else []),success)
            return json.loads(r.stdout) if success else r
        loaded=cmd(source,'list');assert loaded
        passed('read-only C/E/X/Y/Z source catalog (no ST.DB rewriting)')
        out=root/'edited.raw';r=cmd(source,'edit',out,imports);actual=raw_files(out)
        expected=original.copy();del expected['E/zero.bin'];expected['E/renamed.txt']=expected.pop('E/readme.txt');expected['E/Work/Moved/renamed.txt']=original['E/readme.txt']
        expected['E/Work/host import/new.bin']=b'NEW_HOST_CONTENT';expected['E/Work/host import/zero-length.bin']=b'';expected['E/UDATA/12345678/save.bin']=b'NEW_HOST_CONTENT'
        assert actual==expected
        passed('new folder + rename + copy + move + delete + recursive import + replacement in one pending save')
        inv=inventory(r['report']);assert inv['E/renamed.txt']['attributes']==0x21 and inv['E/Work/Moved/renamed.txt']['attributes']==0x21
        assert inv['E/Work/host import/empty']['directory'] and inv['E/Work/host import/zero-length.bin']['size']==0
        inspect_oracle(a.oracle,out,inv);scan_allocated_for_marker(out,BLOAT)
        passed('independent TEST12 reader validates all file hashes/attributes; deleted/free/slack markers excluded')
        backup=Path(r['backup']);assert backup.parent==root/'Backups' and backup.name.startswith('HDD-')
        assert (backup/'E/readme.txt').read_bytes()==original['E/readme.txt'] and (backup/'E/zero.bin').read_bytes()==original['E/zero.bin']
        assert (backup/'E/UDATA/12345678/save.bin').read_bytes()==original['E/UDATA/12345678/save.bin']
        assert not (backup/'E/TDATA').exists() and not (backup/'C').exists() and (backup/'complete.json').is_file()
        assert not list(backup.glob('.xhc-transaction-*')) and (root/'Backups/old-recovery/keep').read_bytes()==b'KEEP_OLD_BACKUP'
        passed('only affected original files backed up in Backups/HDD-unique; old recovery preserved; work removed on success')
        assert all(actual[p]==b for p,b in original.items() if p not in ('E/zero.bin','E/readme.txt','E/UDATA/12345678/save.bin'))
        passed('unrelated system/cache/music/ST.DB/legacy recovery data remains byte-identical')
        exported=root/'export';cmd(source,'export',exported)
        assert (exported/'readme.txt').read_bytes()==original['E/readme.txt'] and (exported/'UDATA/12345678/save.bin').read_bytes()==original['E/UDATA/12345678/save.bin']
        assert (root/'export.hdd-export.json').is_file() and not list(root.glob('.xhc-export-*'))
        passed('selected files/directories export to NEW host folder and verified inventory')
        cmd(source,'export',exported,success=False);assert (exported/'readme.txt').read_bytes()==original['E/readme.txt']
        passed('export refuses existing destination without merging/overwriting')
        folder=root/'folder output';r=cmd(source,'folder',folder,imports)
        assert (folder/'E/Work/host import/new.bin').read_bytes()==b'NEW_HOST_CONTENT';assert (folder/'Images/ce-sync-baseline.tsv').is_file();cmd(folder,'list')
        passed('Folder-HDD output images/mirrors/baseline reopen consistently')
        raw2=root/'from-folder.raw';r=cmd(folder,'simple',raw2);assert raw_files(raw2)==actual;inspect_oracle(a.oracle,raw2,inventory(r['report']))
        passed('Folder-HDD edits export to clean RAW with all existing files unchanged')
        (folder/'Images/ce-image-dirty.flag').write_text('dirty');cmd(folder,'list',success=False);(folder/'Images/ce-image-dirty.flag').unlink()
        passed('dirty Folder-HDD refused without choosing a side')
        for op in ('stale','cancel','changed-import'):
            target=root/(op+'.raw');before=set((root/'Backups').iterdir());p=cmd(source,op,target,imports,success=False);created=set((root/'Backups').iterdir())-before
            assert not target.exists() and len(created)==1 and 'retained at:' in p.stderr and (next(iter(created))/'hdd-failure.txt').exists()
            passed(op+' refuses publication and reports exact retained recovery workspace')
            if op=='changed-import':(imports/'new.bin').write_bytes(b'NEW_HOST_CONTENT')
        cmd(source,'edit',source,success=False);cmd(source,'edit',out,success=False)
        passed('source aliases and existing output refused')
        target=root/'stale-export';p=cmd(source,'export-stale',target,success=False)
        assert not target.exists() and 'recovery workspace retained' in p.stderr
        passed('stale export retains private work and reports exact path')
        if os.name!='nt':
            alias=root/'alias-parent';alias.mkdir();(alias/'Backups').symlink_to(root/'Backups',target_is_directory=True)
            cmd(source,'edit',alias/'new.raw',success=False);assert not (alias/'new.raw').exists()
            passed('symlinked backup-root redirection refused')
        with source.open('rb') as f:assert hashlib.file_digest(f,'sha256').hexdigest()==original_hash
        passed('complete source SHA-256 unchanged after successful and failed edits/exports')
        qemu=shutil.which('qemu-img')
        if qemu:
            qcow=root/'source.qcow2';run_cmd([qemu,'convert','-f','raw','-O','qcow2',source,qcow]);new=root/'edited.qcow2';cmd(qcow,'qcow2',new,imports)
            decoded=root/'qcow.raw';run_cmd([qemu,'convert','-f','qcow2','-O','raw',new,decoded]);assert raw_files(decoded)==actual;run_cmd([qemu,'check',new]);passed('real QCOW2 HDD edit round trip')
        else:print('SKIP: real QCOW2 HDD edit (qemu-img not installed).',flush=True)
    print('PASS:',count,'HDD Directory integration scenarios')
if __name__=='__main__':main()
