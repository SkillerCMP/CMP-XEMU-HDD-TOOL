#!/usr/bin/env python3
"""Independent sparse FATX fixtures and create-new regression tests.
The application is C++; Python is used only as a test/packaging harness.
"""
import argparse
import base64
import hashlib
import json
import os
from pathlib import Path
import shutil
import signal
import struct
import subprocess
import tempfile
import time

SIZE = 0x1DD156000
LAYOUT = [('C',0x8CA80000,0x1F400000),('E',0xABE80000,0x1312D6000),
          ('X',0x80000,0x2EE00000),('Y',0x2EE80000,0x2EE00000),('Z',0x5DC80000,0x2EE00000)]
BLOAT = b'XHC_DELETED_REMNANT_724118693_DO_NOT_COPY'
TIMES = [0x1234, 0x5821, 0x2234, 0x5621, 0x1034, 0x5421]


def file(name, payload=b'', attrs=0x20):
    return {'name': name, 'data': payload, 'attrs': attrs, 'times': TIMES.copy()}


def directory(name, children):
    return {'name': name, 'children': children, 'attrs': 0x10, 'times': TIMES.copy()}


def geometry(size, cluster):
    entries = size // cluster + 1
    bits = 16 if entries < 0xfff0 else 32
    fat_bytes = (entries * (bits//8) + 4095) & ~4095
    return bits, fat_bytes, 4096+fat_bytes, (size-4096-fat_bytes)//cluster


def make_fixture(path, extra_tree=None, e_tree=None):
    payload = bytes((i*29+7)&255 for i in range(120031))
    tree = {
        'C': [file('xboxdash.xbe', b'SYNTHETIC_TEST_XBE_NOT_EXECUTABLE\0'),
              directory('Empty', []), directory('Many', [file('f%03d'%i, bytes([i&255])) for i in range(270)])],
        'E': [directory('TDATA', [directory('fffe0000', [directory('music', [
               file('ST.DB', b'SYNTHETIC_ST_DB\0'), directory('0004', [file('0004001f.wma', payload)])]),
               directory('music.CSB-backup-old', [file('database.bak', b'KEEP_LIVE_RECOVERY')])]),
               directory('CSBBACKUP', [directory('old-transaction', [file('database.bak', b'KEEP_UNCERTAIN_RECOVERY')])])]),
              directory('UDATA', [directory('12345678', [file('save.bin', payload*3)])]),
              file('zero.bin', b'\0'*16000),file('empty.bin',b''),file('readme.txt',b'a\r\nb\r\n',0x21)],
        'X': [file('cache.bin',b'LIVE_X_CACHE')],
        'Y': [file('cache.bin',b'LIVE_Y_CACHE')],
        'Z': [file('cache.bin',b'LIVE_Z_CACHE')],
    }
    if e_tree is not None:
        tree['E'] = e_tree
    if extra_tree:
        tree['E'].append(extra_tree)
    expected = {}
    meta = {}
    with open(path,'xb') as fp:
        fp.truncate(SIZE)
        fp.seek(0);fp.write(b'XHC_REQUIRED_SYSTEM_METADATA\0')
        for letter,offset,size in LAYOUT:
            cluster = 4096 if letter=='E' else 16384
            bits,fatsize,data_start,count = geometry(size,cluster)
            fat = bytearray(fatsize)
            def setfat(c, value):
                struct.pack_into('<H' if bits==16 else '<I',fat,c*(bits//8),value&((1<<bits)-1))
            setfat(0,(1<<bits)-1)
            root=directory(letter,tree[letter]);root['times']=[0]*6
            next_cluster=2
            locations={}
            def alloc(n, p):
                nonlocal next_cluster
                isdir='children' in n
                length=(len(n['children'])+2)*64 if isdir else len(n['data'])
                needed=(length+cluster-1)//cluster
                if isdir: needed+=1  # allocated trailing directory slack, never live data
                if not isdir and not length: needed=1 # source preallocation for an empty live file
                chain=list(range(next_cluster,next_cluster+needed*2,2));next_cluster+=needed*2
                assert not chain or chain[-1]<count
                n['chain']=chain
                for i,c in enumerate(chain):setfat(c,chain[i+1] if i+1<len(chain) else (1<<bits)-1)
                locations[p]={'chain':chain,'node':n}
                expected[p]={'directory':isdir,'size':0 if isdir else len(n['data']),
                             'attributes':n['attrs'],'fatx_timestamps':n['times'],
                             'sha256':'' if isdir else hashlib.sha256(n['data']).hexdigest().upper()}
                if isdir:
                    for child in n['children']:alloc(child,p+'/'+child['name'])
            alloc(root,letter)
            header=bytearray(b'\xff'*4096)
            struct.pack_into('<4I',header,0,0x58544146,0x12345600+ord(letter),cluster//512,root['chain'][0])
            fp.seek(offset);fp.write(header);fp.seek(offset+4096);fp.write(fat)
            def write_node(n,p):
                if 'children' in n:
                    blob=bytearray(b'\xff'*(len(n['chain'])*cluster))
                    for index,child in enumerate(n['children']):
                        pos=index*64;name=child['name'].encode('ascii');assert len(name)<=42
                        blob[pos]=len(name);blob[pos+1]=child['attrs'];blob[pos+2:pos+2+len(name)]=name
                        struct.pack_into('<II6H',blob,pos+44,child['chain'][0] if child['chain'] else 0,
                                         0 if 'children' in child else len(child['data']),*child['times'])
                        locations[p+'/'+child['name']]['entry_offset']=offset+data_start+(n['chain'][pos//cluster]-1)*cluster+pos%cluster
                    # A deleted directory record with stale name/cluster bytes is not live.
                    pos=len(n['children'])*64;blob[pos]=0xe5;blob[pos+2:pos+2+len(BLOAT)]=BLOAT
                    struct.pack_into('<II',blob,pos+44,15000,1024)
                    # A directory terminator precedes stale contents in the trailing chain.
                    blob[pos+64]=0xff
                    for c in n['chain']:
                        start=n['chain'].index(c)*cluster
                        fp.seek(offset+data_start+(c-1)*cluster);fp.write(blob[start:start+cluster])
                    last=n['chain'][-1]
                    fp.seek(offset+data_start+(last-1)*cluster+cluster//2);fp.write(BLOAT)
                    for child in n['children']:write_node(child,p+'/'+child['name'])
                else:
                    blob=n['data']
                    for i,c in enumerate(n['chain']):
                        piece=blob[i*cluster:(i+1)*cluster]
                        fp.seek(offset+data_start+(c-1)*cluster);fp.write(piece)
                        if len(piece)+len(BLOAT)<cluster:
                            fp.write(BLOAT) # source file slack, including preallocated empty file
            write_node(root,letter)
            # Known-free nonzero data is the "deleted bloat" regression fixture.
            bloat_offset=offset+data_start+(15001-1)*cluster
            fp.seek(bloat_offset);fp.write(((BLOAT+b'\xAA'*200)*32)[:cluster])
            meta[letter]={'offset':offset,'size':size,'cluster':cluster,'bits':bits,
                          'fat_offset':offset+4096,'data_offset':offset+data_start,
                          'root':root['chain'],'locations':locations,'bloat_offset':bloat_offset}
        fp.flush();os.fsync(fp.fileno())
    return expected,meta


def run_cmd(argv, success=True):
    p=subprocess.run([str(x) for x in argv],stdout=subprocess.PIPE,stderr=subprocess.PIPE,text=True,timeout=240)
    if success and p.returncode:
        raise AssertionError('Command failed:\n'+repr(argv)+'\n'+p.stdout+'\n'+p.stderr)
    if not success and p.returncode==0:
        raise AssertionError('Unsafe command unexpectedly succeeded: '+repr(argv))
    return p


def inventory(report):
    data=json.loads(Path(report).read_text())
    return {bytes.fromhex(n['path_hex']).decode('ascii'): {k:n[k] for k in ('directory','size','attributes','fatx_timestamps','sha256')}
            for n in data['files']}


def inspect_oracle(oracle, raw, expected):
    p=run_cmd([oracle,raw]);actual={}
    for line in p.stdout.splitlines():
        name,size,sha=line.split('\t');actual[bytes.fromhex(name).decode('ascii')]=(int(size),sha)
    wanted={name:(n['size'],n['sha256']) for name,n in expected.items() if not n['directory']}
    assert actual==wanted, 'Independent TEST12 parser/streamer mismatch'


def scan_allocated_for_marker(path, marker):
    # This is independent of converter verification. Sparse holes contain zero.
    with open(path,'rb') as f:
        size=os.fstat(f.fileno()).st_size;pos=0
        while pos<size:
            try:
                data=os.lseek(f.fileno(),pos,os.SEEK_DATA)
                end=os.lseek(f.fileno(),data,os.SEEK_HOLE)
            except OSError as e:
                if e.errno==6: return
                data=pos;end=size
            f.seek(data);tail=b''
            while f.tell()<end:
                chunk=f.read(min(1024*1024,end-f.tell()))
                assert chunk
                assert marker not in tail+chunk, 'Deleted/slack marker survived clean conversion'
                tail=chunk[-len(marker):]
            pos=end


def test_all(args):
    checks=0
    def passed(label):
        nonlocal checks
        checks+=1;print('PASS',checks,label,flush=True)
    with tempfile.TemporaryDirectory(prefix='xhc-integration-') as tmp:
        root=Path(tmp);source=root/'source.raw';expected,meta=make_fixture(source)
        inspect_oracle(args.oracle,source,expected)
        original_alloc=source.stat().st_blocks*512
        with source.open('rb') as fp: original_hash=hashlib.file_digest(fp,'sha256').hexdigest()
        folder=root/'Folder output with spaces'
        run_cmd([args.binary,'image-to-folder','--source',source,'--output',folder,'--offline'])
        assert inventory(folder/'Converter-Metadata/conversion-report.json')==expected
        passed('raw -> Folder-HDD: every live file, directory, attribute, timestamp and SHA-256')
        for p in (folder/'Images/C.img',folder/'Images/E.img',folder/'Cache/xbox_cache.img'):
            scan_allocated_for_marker(p,BLOAT)
        passed('deleted entry/free-cluster/file-tail/directory-slack markers absent from all output images')
        assert (folder/'E/TDATA/CSBBACKUP/old-transaction/database.bak').read_bytes()==b'KEEP_UNCERTAIN_RECOVERY'
        assert (folder/'E/TDATA/fffe0000/music.CSB-backup-old/database.bak').read_bytes()==b'KEEP_LIVE_RECOVERY'
        passed('live CSB and legacy recovery files preserved')
        raw=root/'roundtrip.raw'
        run_cmd([args.binary,'folder-to-image','--source',folder,'--output',raw,'--raw-output','--offline'])
        assert inventory(str(raw)+'.conversion.json')==expected
        inspect_oracle(args.oracle,raw,expected);scan_allocated_for_marker(raw,BLOAT)
        assert raw.stat().st_blocks*512 < original_alloc
        passed('Folder-HDD -> raw: independent TEST12 production parser verifies all payloads; allocation shrinks')
        run_cmd([args.binary,'analyze','--source',folder,'--offline'])
        passed('clean generated baseline/image/mirrors accepted on reopen')
        raw2=root/'clean-again.raw';run_cmd([args.binary,'clean-image','--source',raw,'--output',raw2,'--raw-output','--offline'])
        assert inventory(str(raw2)+'.conversion.json')==expected
        passed('second clean conversion is content-stable')
        # Source image is unmodified, including the deleted marker.
        with source.open('rb') as f:f.seek(meta['E']['bloat_offset']);assert f.read(len(BLOAT))==BLOAT
        with source.open('rb') as fp: assert hashlib.file_digest(fp,'sha256').hexdigest()==original_hash
        passed('complete source image SHA-256 unchanged, including deleted data')
        existing=root/'existing.raw';existing.write_bytes(b'DO_NOT_OVERWRITE')
        run_cmd([args.binary,'clean-image','--source',source,'--output',existing,'--raw-output','--offline'],False)
        assert existing.read_bytes()==b'DO_NOT_OVERWRITE';passed('existing output refused without mutation')
        run_cmd([args.binary,'clean-image','--source',source,'--output',source,'--raw-output','--offline'],False);passed('source == output refused')
        run_cmd([args.binary,'image-to-folder','--source',source,'--output',root/'no-confirm'],False);passed('offline acknowledgment required')
        run_cmd([args.binary,'folder-to-image','--source',folder,'--output',folder/'bad.qcow2','--raw-output','--offline'],False);passed('nested destination refused')
        (folder/'Images/ce-image-dirty.flag').write_text('DIRTY')
        run_cmd([args.binary,'folder-to-image','--source',folder,'--output',root/'dirty.raw','--raw-output','--offline'],False)
        (folder/'Images/ce-image-dirty.flag').unlink();passed('dirty Folder-HDD refused')
        mirror=folder/'E/TDATA/CSBBACKUP/old-transaction/database.bak';data=mirror.read_bytes();st=mirror.stat();mirror.write_bytes(b'X'*len(data));os.utime(mirror,ns=(st.st_atime_ns,st.st_mtime_ns))
        run_cmd([args.binary,'folder-to-image','--source',folder,'--output',root/'conflict.raw','--raw-output','--offline'],False)
        mirror.write_bytes(data);os.utime(mirror,ns=(st.st_atime_ns,st.st_mtime_ns));passed('same-size/same-mtime mirror corruption caught by SHA-256')
        baseline=folder/'Images/ce-sync-baseline.tsv';baseline_data=baseline.read_bytes();baseline.unlink()
        run_cmd([args.binary,'folder-to-image','--source',folder,'--output',root/'missing-base.raw','--raw-output','--offline'],False)
        baseline.write_bytes(baseline_data);passed('missing synchronization baseline refused')
        if os.name!='nt':
            link=root/'link.raw';link.symlink_to(source)
            run_cmd([args.binary,'image-to-folder','--source',link,'--output',root/'link-output','--offline'],False);passed('symlink input refused')
            with source.open('r+b') as writer:
                run_cmd([args.binary,'image-to-folder','--source',source,'--output',root/'live-output','--offline'],False)
            passed('detected open writable source handle refused')
        def corrupt(offset,new,name,needle=None):
            with source.open('r+b') as f:f.seek(offset);old=f.read(len(new));f.seek(offset);f.write(new)
            out=root/('refuse-'+name)
            p=run_cmd([args.binary,'image-to-folder','--source',source,'--output',out,'--offline'],False)
            assert not out.exists()
            if needle:assert needle.lower() in p.stderr.lower(),p.stderr
            with source.open('r+b') as f:f.seek(offset);f.write(old)
            passed(name+' refused without publishing')
        e=meta['E'];loc=e['locations'];first=loc['E/TDATA/fffe0000/music/0004/0004001f.wma']['chain'][0]
        corrupt(e['offset'],b'BAD!','missing-FATX','Missing FATX')
        corrupt(e['offset']+8,struct.pack('<I',3),'invalid-cluster-size')
        corrupt(e['fat_offset']+first*4,struct.pack('<I',first),'file-chain-cycle','cyclic')
        corrupt(e['fat_offset']+first*4,b'\0'*4,'live-chain-marked-free')
        corrupt(e['fat_offset']+1000*4,struct.pack('<I',0xffffffff),'allocated-orphan','unreachable')
        cross=loc['E/readme.txt']['entry_offset']+44
        corrupt(cross,struct.pack('<I',first),'crosslinked-file')
        entry=loc['E/readme.txt']['entry_offset']
        corrupt(entry+48,struct.pack('<I',0xffffffff),'truncated-file-chain')
        corrupt(entry,b'\x2b','too-long-name')
        corrupt(entry,bytes([2,0x20])+b'..','traversal-name')
        corrupt(entry,bytes([3,0x20])+b'NUL','reserved-host-name')
        corrupt(0,b'****PARTINFO****','custom-partition-table')
        # An 8-GiB conventional container is accepted only when its tail is zero.
        with source.open('r+b') as f:f.truncate(0x200000000)
        large=root/'eight-gib';run_cmd([args.binary,'image-to-folder','--source',source,'--output',large,'--offline'])
        assert inventory(large/'Converter-Metadata/conversion-report.json')==expected;passed('8-GiB zero unpartitioned tail accepted and normalized')
        corrupt(SIZE,b'FATX','nonzero-extended-tail')
        with source.open('r+b') as f:f.truncate(SIZE)
        # Helper failure path: protocol facade, not a real QCOW2 conversion test.
        qheader=bytearray(104);struct.pack_into('>II',qheader,0,0x514649fb,3);struct.pack_into('>Q',qheader,24,SIZE);struct.pack_into('>I',qheader,100,104)
        fake_source=root/'header-only.qcow2';fake_source.write_bytes(qheader)
        helper=root/'helper with spaces.py'
        helper.write_text('#!/usr/bin/env python3\nimport sys,json\nassert "-U" not in sys.argv\nif sys.argv[1]=="info": print(json.dumps({"format":"qcow2","virtual-size":'+str(SIZE)+'}))\nelse: sys.exit(7)\n')
        helper.chmod(0o755)
        failure=root/'helper-failed';p=run_cmd([args.binary,'image-to-folder','--source',fake_source,'--output',failure,'--offline','--qemu-img',helper],False)
        assert 'exit 7' in p.stderr and 'retained at:' in p.stderr and not failure.exists()
        assert fake_source.read_bytes()==qheader;passed('helper failure retains workspace, reports exact location, leaves source unchanged (facade)')
        if os.name!='nt':
            marker=root/'child.pid';helper.write_text('#!/usr/bin/env python3\nimport sys,json,time,os\nif sys.argv[1]=="info": print(json.dumps({"format":"qcow2","virtual-size":'+str(SIZE)+'}))\nelse:\n open('+repr(str(marker))+',"w").write(str(os.getpid()))\n time.sleep(60)\n')
            proc=subprocess.Popen([str(args.binary),'image-to-folder','--source',str(fake_source),'--output',str(root/'cancelled'),'--offline','--qemu-img',str(helper)],stdout=subprocess.PIPE,stderr=subprocess.PIPE,text=True)
            for _ in range(150):
                if marker.exists():break
                time.sleep(.02)
            assert marker.exists();proc.send_signal(signal.SIGINT);_,err=proc.communicate(timeout=10)
            assert proc.returncode!=0 and 'Cancelled' in err and not (root/'cancelled').exists()
            try:os.kill(int(marker.read_text()),0);raise AssertionError('Helper survived cancellation')
            except ProcessLookupError:pass
            passed('cancellation terminates/reaps helper and never publishes (facade)')
        qemu=shutil.which('qemu-img')
        if not qemu:
            if args.require_qemu:raise AssertionError('Real qemu-img is REQUIRED by this build but missing')
            print('SKIP: real QCOW2 helper integration (qemu-img not installed); no claim of real QCOW2 execution.',flush=True)
        else:
            qcow=root/'source.qcow2';run_cmd([qemu,'convert','-f','raw','-O','qcow2',source,qcow]);before=hashlib.sha256(qcow.read_bytes()).hexdigest()
            qfolder=root/'from-qcow';run_cmd([args.binary,'image-to-folder','--source',qcow,'--output',qfolder,'--offline','--qemu-img',qemu])
            assert inventory(qfolder/'Converter-Metadata/conversion-report.json')==expected;passed('REAL qemu-img QCOW2 -> Folder-HDD')
            result=root/'roundtrip.qcow2';run_cmd([args.binary,'folder-to-image','--source',qfolder,'--output',result,'--offline','--qemu-img',qemu]);assert inventory(str(result)+'.conversion.json')==expected
            result_raw=root/'decoded.raw';run_cmd([qemu,'convert','-f','qcow2','-O','raw',result,result_raw]);inspect_oracle(args.oracle,result_raw,expected);scan_allocated_for_marker(result_raw,BLOAT);passed('REAL qemu-img Folder-HDD -> QCOW2 with independent decoded-file and bloat checks')
            assert hashlib.sha256(qcow.read_bytes()).hexdigest()==before;passed('real input QCOW2 container hash unchanged')
            run_cmd([qemu,'snapshot','-c','kept-in-original',qcow]);snap_before=hashlib.sha256(qcow.read_bytes()).hexdigest()
            run_cmd([args.binary,'clean-image','--source',qcow,'--output',root/'refused.qcow2','--offline','--qemu-img',qemu],False)
            cleaned=root/'no-snapshots.qcow2';run_cmd([args.binary,'clean-image','--source',qcow,'--output',cleaned,'--offline','--drop-snapshots','--qemu-img',qemu]);info=json.loads(run_cmd([qemu,'info','--output=json',cleaned]).stdout)
            assert not info.get('snapshots') and not info.get('backing-filename') and hashlib.sha256(qcow.read_bytes()).hexdigest()==snap_before
            passed('real snapshot acknowledgment, standalone current-file export and original snapshot preservation')
            backed=root/'backed.qcow2';run_cmd([qemu,'create','-f','qcow2','-F','qcow2','-b',qcow,backed]);run_cmd([args.binary,'image-to-folder','--source',backed,'--output',root/'backed-output','--offline','--qemu-img',qemu],False);passed('real backing-chain source refused')
    print('PASS:',checks,'integration scenarios',flush=True)

if __name__=='__main__':
    ap=argparse.ArgumentParser();ap.add_argument('--binary',type=Path,required=True);ap.add_argument('--oracle',type=Path,required=True);ap.add_argument('--require-qemu',action='store_true')
    test_all(ap.parse_args())
