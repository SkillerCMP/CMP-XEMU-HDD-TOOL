#!/usr/bin/env python3
"""Standalone CSB pipeline with independent fixtures; not a Docker build gate."""
from pathlib import Path
import argparse, hashlib, json, os, shutil, struct, tempfile
from integration import (make_fixture, file, directory, run_cmd, inventory,
                         inspect_oracle, scan_allocated_for_marker, geometry, LAYOUT, BLOAT)


def database():
    records = [(0,'Original',[(0,5000,'First'),(1,6000,'Second'),(2,7000,'Third')]),
               (1,'Other Music',[(30,8000,'Other A'),(31,9000,'Other B')]),
               (3,'Third Music',[(40,10000,'Keep me')])]
    b=bytearray(0xCA00+3*512)
    struct.pack_into('<III',b,0,1,3,8)
    struct.pack_into('<III',b,12,3,0,0)
    struct.pack_into('<I',b,0x19C,50)
    def name(off,s):
        text=s.encode('utf-16le');b[off:off+len(text)]=text
    for i,(owner,label,songs) in enumerate(records):
        st=512+i*512;group=0xCA00+i*512
        struct.pack_into('<IIII',b,st,0x21371,owner,len(songs),i)
        struct.pack_into('<I',b,st+0x15c,sum(t[1] for t in songs));name(st+0x160,label)
        struct.pack_into('<IIII',b,group,0x31073,owner,0,1)
        for j,(serial,ms,title) in enumerate(songs):
            struct.pack_into('<I',b,group+0x10+4*j,(owner<<16)|serial)
            struct.pack_into('<I',b,group+0x28+4*j,ms);name(group+0x40+64*j,title)
    return bytes(b),records


def raw_files(raw):
    files={}
    with Path(raw).open('rb') as f:
        for letter,offset,size in LAYOUT:
            f.seek(offset);sig,vid,spc,root=struct.unpack('<4I',f.read(16));assert sig==0x58544146
            cluster=spc*512;bits,fatlen,data,count=geometry(size,cluster)
            f.seek(offset+4096);fat=f.read(fatlen)
            def chain(first):
                result=[];c=first
                while c:
                    assert c not in result and c<=count;result.append(c)
                    c=struct.unpack_from('<H' if bits==16 else '<I',fat,c*(bits//8))[0]
                    if c>=(0xfff8 if bits==16 else 0xfffffff8):break
                return result
            def contents(first):
                b=bytearray()
                for c in chain(first):f.seek(offset+data+(c-1)*cluster);b+=f.read(cluster)
                return bytes(b)
            def walk(first,prefix):
                b=contents(first)
                for i in range(0,len(b),64):
                    length=b[i]
                    if length in (0,255):break
                    if length==229:continue
                    name=b[i+2:i+2+length].decode('ascii');attr=b[i+1]
                    start,n=struct.unpack_from('<II',b,i+44);path=prefix+'/'+name
                    if attr&16:walk(start,path)
                    else:files[path]=contents(start)[:n] if start else b''
            walk(root,letter)
    return files


def run(args):
    count=0
    def passed(s):
        nonlocal count
        count+=1;print('PASS',count,s,flush=True)
    with tempfile.TemporaryDirectory(prefix='xhc-csb-integration-') as t:
        root=Path(t);source=root/'source with spaces.raw'
        db,records=database();music=[file('ST.DB',db)]
        for owner,label,songs in records:
            music.append(directory(f'{owner:04X}',[file(f'{owner:04X}{serial:04X}.WMA',f'UNCHANGED WMA {owner} {serial}'.encode()) for serial,_,_ in songs]+([file('keep.nfo',b'UNKNOWN_BUT_LIVE')] if owner==0 else [])))
        music.append(directory('0004',[file('00040050.WMA',b'ORPHAN_KEEP')]))
        tree=[directory('TDATA',[directory('fffe0000',[directory('music',music),directory('music.CSB-backup-old',[file('database.bak',b'KEEP_OLD_RECOVERY')])]),directory('CSBBACKUP',[file('recover.bin',b'KEEP_LIVE_RECOVERY')])]),directory('UDATA',[file('save.bin',b'KEEP_GAME_SAVE')])]
        expected,_=make_fixture(source,e_tree=tree)
        (root/'Backups'/'old-transaction').mkdir(parents=True);(root/'Backups'/'old-transaction'/'keep.txt').write_text('KEEP')
        def cmd(src,op,out=None,audio=None,success=True):
            p=run_cmd([args.driver,src,op]+([out] if out else [])+([audio] if audio else []),success)
            return json.loads(p.stdout) if success else p
        info=cmd(source,'list');assert info['writable'] and len(info['soundtracks'])==4 and info['soundtracks'][-1]['orphan']
        passed('read FATX catalog; malformed header is repairable; orphan is read-only')
        with source.open('rb') as f:original=hashlib.file_digest(f,'sha256').hexdigest()
        repaired=root/'repair.raw';r=cmd(source,'repair',repaired);data=raw_files(repaired)
        assert struct.unpack_from('<III',data['E/TDATA/fffe0000/music/ST.DB'],12)==(0,1,3)
        assert data['E/TDATA/fffe0000/music/ST.DB'][512:]==db[512:]
        passed('TEST12 header repair; bytes after offset 0x200 unchanged')
        backup=Path(r['backup']);assert backup.parent==root/'Backups'
        assert (backup/'database.bak').read_bytes()==db and (backup/'0000'/'00000000.WMA').read_bytes()==b'UNCHANGED WMA 0 0'
        assert (backup/'0000'/'keep.nfo').read_bytes()==b'UNKNOWN_BUT_LIVE'
        assert not (backup/'0001').exists() and (backup/'complete.json').exists()
        assert not list(backup.glob('.xhc-transaction-*')) and (root/'Backups/old-transaction/keep.txt').read_text()=='KEEP'
        passed('dedicated Backups/CSB transaction; affected soundtrack only; old backups retained')
        scan_allocated_for_marker(repaired,BLOAT);inspect_oracle(args.oracle,repaired,inventory(r['report']))
        passed('deleted/free/slack markers absent; independent TEST12 reader verifies every live file')
        output=root/'edited.raw';r=cmd(source,'reorder',output);edited=raw_files(output)
        assert 'E/TDATA/fffe0000/music/0000/00000001.WMA' not in edited
        for path,b in data.items():
            if path not in ('E/TDATA/fffe0000/music/ST.DB','E/TDATA/fffe0000/music/0000/00000001.WMA'):assert edited[path]==b
        info=cmd(output,'list');s=info['soundtracks'][0];assert s['name']=='Edited Soundtrack' and [x['id'] for x in s['songs']]==[2,0] and s['songs'][1]['title']=='Renamed first'
        passed('rename/reorder/remove preserve IDs and unrelated/unknown/recovery/cache files')
        inspect_oracle(args.oracle,output,inventory(r['report']));scan_allocated_for_marker(output,BLOAT)
        deleted=root/'deleted.raw';r=cmd(source,'delete',deleted);deleted_files=raw_files(deleted)
        assert not any(p.startswith('E/TDATA/fffe0000/music/0000/') for p in deleted_files)
        info=cmd(deleted,'list');assert all(x['id']!=0 for x in info['soundtracks']) and info['next_song_id']==50 and info['next_soundtrack_id']==8
        assert (Path(r['backup'])/'0000'/'keep.nfo').exists()
        passed('whole soundtrack removed only from new output; backed up; monotonic counters retained')
        folder=root/'Folder CSB';r=cmd(source,'folder',folder)
        assert (folder/'Images/ce-sync-baseline.tsv').is_file() and not (folder/'Images/ce-image-dirty.flag').exists()
        assert (folder/'E/TDATA/fffe0000/music/0000/00000000.WMA').read_bytes()==b'UNCHANGED WMA 0 0'
        info=cmd(folder,'list');assert info['soundtracks'][0]['name']=='Edited Soundtrack'
        passed('image-to-Folder CSB rebuilds images, mirrors and synchronization baseline')
        folder2=root/'Folder CSB 2';cmd(folder,'folder',folder2);cmd(folder2,'list')
        passed('Folder-to-Folder CSB save reopens without synchronization conflict')
        (folder/'Images/ce-image-dirty.flag').write_text('dirty')
        cmd(folder,'list',success=False);(folder/'Images/ce-image-dirty.flag').unlink()
        passed('dirty Folder-HDD refused')
        for op in ('stale','cancel'):
            out=root/(op+'.raw');before=set((root/'Backups').iterdir());p=cmd(source,op,out,success=False)
            created=set((root/'Backups').iterdir())-before
            assert not out.exists() and len(created)==1 and 'retained at:' in p.stderr
            assert (next(iter(created))/'csb-failure.txt').exists()
            passed(op+' save refused; source retained and exact recovery paths reported')
        cmd(source,'repair',repaired,success=False);cmd(source,'repair',source,success=False)
        passed('existing output and source aliases refused')
        out=root/'bad-new-id.raw';before=set((root/'Backups').iterdir());cmd(source,'invalid-id',out,success=False)
        assert not out.exists() and set((root/'Backups').iterdir())==before
        passed('backend independently rejects a new soundtrack ID below the database counter')
        if os.name!='nt':
            alias_root=root/'alias-parent';alias_root.mkdir();(alias_root/'Backups').symlink_to(root/'Backups',target_is_directory=True)
            out=alias_root/'out.raw';cmd(source,'repair',out,success=False)
            assert not out.exists()
            passed('symlinked Backups root refused rather than redirecting recovery data')
        qemu=shutil.which('qemu-img')
        if qemu:
            qcow=root/'source.qcow2';run_cmd([qemu,'convert','-f','raw','-O','qcow2',source,qcow])
            result=root/'csb-clean.qcow2';cmd(qcow,'qcow2',result)
            info=cmd(result,'list');assert info['soundtracks'][0]['songs'][0]['title']=='First'
            run_cmd([qemu,'check',result]);decoded=root/'decoded.raw';run_cmd([qemu,'convert','-f','qcow2','-O','raw',result,decoded])
            assert raw_files(decoded)==data;scan_allocated_for_marker(decoded,BLOAT)
            passed('real QCOW2 CSB source/output, hash/clean verification and standalone helper round trip')
        else:print('SKIP: real QCOW2 CSB round trip (qemu-img is not installed).',flush=True)
        ffmpeg=shutil.which('ffmpeg')
        if ffmpeg:
            audio=root/'new song é.wav';run_cmd([ffmpeg,'-v','error','-f','lavfi','-i','sine=frequency=440:duration=1','-ac','2',audio])
            new=root/'audio.raw';r=cmd(source,'create',new,audio);info=cmd(new,'list')
            added=next(s for s in info['soundtracks'] if s['id']==8)
            assert len(added['songs'])==1 and added['songs'][0]['id']==50 and added['songs'][0]['duration']>0
            assert info['next_song_id']==51 and info['next_soundtrack_id']==9
            files=raw_files(new);wma=files['E/TDATA/fffe0000/music/0008/00080032.WMA'];assert b'\x61\x01\x02\x00\x44\xac\x00\x00' in wma
            for path,b in data.items():
                if path!='E/TDATA/fffe0000/music/ST.DB':assert files[path]==b
            scan_allocated_for_marker(new,BLOAT);inspect_oracle(args.oracle,new,inventory(r['report']))
            passed('real external FFmpeg, Unicode source path, global serial 50, WMA2 stereo 44.1kHz and positive duration')
            before=set((root/'Backups').iterdir());out=root/'bad-helper.raw';cmd(source,'bad-ffmpeg',out,audio,success=False)
            created=set((root/'Backups').iterdir())-before;assert not out.exists() and len(created)==1 and all((d/'database.bak').read_bytes()==db for d in created)
            passed('missing FFmpeg stops publication and retains verified backups')
            empty=root/'empty-music.raw';make_fixture(empty,e_tree=[]);new=root/'initial-music.raw'
            r=cmd(empty,'create',new,audio);info=cmd(new,'list');assert len(info['soundtracks'])==1
            assert info['soundtracks'][0]['id']==0 and info['soundtracks'][0]['songs'][0]['id']==0
            assert info['next_song_id']==1 and info['next_soundtrack_id']==1
            assert not (Path(r['backup'])/'database.bak').exists() and not (Path(r['backup'])/'0000').exists()
            assert (Path(r['backup'])/'complete.json').exists()
            inspect_oracle(args.oracle,new,inventory(r['report']));scan_allocated_for_marker(new,BLOAT)
            passed('first soundtrack on a music-free disk initializes global IDs, directories and DB without invented backups')
        else:print('SKIP: real FFmpeg fixture (not installed)',flush=True)
        with source.open('rb') as f:assert hashlib.file_digest(f,'sha256').hexdigest()==original
        passed('complete source disk SHA-256 unchanged after successful and failed CSB saves')
        assert (root/'Backups/old-transaction/keep.txt').read_text()=='KEEP'
    print(f'PASS: {count} standalone CSB integration scenarios',flush=True)

if __name__=='__main__':
    p=argparse.ArgumentParser();p.add_argument('--driver',required=True);p.add_argument('--oracle',required=True);run(p.parse_args())
