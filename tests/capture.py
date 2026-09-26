#!/usr/bin/env python3
"""Optional external uploaded-file preservation test. No user media is bundled.
Builds an independent sparse FATX fixture containing the supplied ZIP's files;
this tests byte preservation, not real-dashboard boot or actual disk health.
"""
import argparse
import hashlib
import json
from pathlib import Path, PurePosixPath
import tempfile
import zipfile
from integration import make_fixture, file, directory, run_cmd, inventory, inspect_oracle, scan_allocated_for_marker, BLOAT


def main():
    ap=argparse.ArgumentParser()
    ap.add_argument('--capture',type=Path,required=True)
    ap.add_argument('--binary',type=Path,required=True)
    ap.add_argument('--oracle',type=Path,required=True)
    ap.add_argument('--report',type=Path,required=True)
    args=ap.parse_args()
    original={};tree={}
    with zipfile.ZipFile(args.capture) as z:
        for item in z.infolist():
            if item.is_dir():continue
            parts=PurePosixPath(item.filename).parts
            if not parts or any(p in ('..','.') for p in parts) or item.filename.startswith('/'):
                raise ValueError('Unsafe ZIP member')
            content=z.read(item);original[item.filename]=(len(content),hashlib.sha256(content).hexdigest().upper())
            node=tree
            for part in parts[:-1]:node=node.setdefault(part,{})
            if parts[-1] in node:raise ValueError('Duplicate capture path')
            node[parts[-1]]=content
    def make(name, children):
        result=[]
        for key,value in sorted(children.items()):
            result.append(make(key,value) if isinstance(value,dict) else file(key,value))
        return directory(name,result)
    with tempfile.TemporaryDirectory(prefix='xhc-capture-test-') as tmp:
        root=Path(tmp);source=root/'capture-fixture.raw'
        expected,_=make_fixture(source,make('UploadedCapture',tree))
        inspect_oracle(args.oracle,source,expected)
        folder=root/'folder';run_cmd([args.binary,'image-to-folder','--source',source,'--output',folder,'--offline'])
        actual_inventory=inventory(folder/'Converter-Metadata/conversion-report.json')
        assert actual_inventory==expected, 'Inventory mismatch: '+repr([k for k in expected if actual_inventory.get(k)!=expected[k]][:10])
        for name,(size,sha) in original.items():
            target=folder/'E/UploadedCapture'/name
            with target.open('rb') as fp:actual=hashlib.file_digest(fp,'sha256').hexdigest().upper()
            assert target.stat().st_size==size and actual==sha
        raw=root/'roundtrip.raw';run_cmd([args.binary,'folder-to-image','--source',folder,'--output',raw,'--raw-output','--offline'])
        assert inventory(str(raw)+'.conversion.json')==expected
        inspect_oracle(args.oracle,raw,expected);scan_allocated_for_marker(raw,BLOAT)
        details={'status':'PASS','scope':'Uploaded files embedded in independent synthetic FATX fixture; raw -> Folder-HDD -> raw; not dashboard runtime or real QCOW2 helper execution',
                 'capture_name':args.capture.name,'capture_sha256':hashlib.sha256(args.capture.read_bytes()).hexdigest().upper(),
                 'captured_files':len(original),'captured_bytes':sum(v[0] for v in original.values()),
                 'all_file_lengths_and_sha256_preserved':True,'independent_TEST12_reader_verified':True,'deleted_and_slack_markers_absent':True,
                 'database_sha256':{n:v[1] for n,v in original.items() if n.lower().endswith('st.db')}}
        args.report.write_text(json.dumps(details,indent=2)+'\n')
        print(json.dumps(details,indent=2),flush=True)

if __name__=='__main__':main()
