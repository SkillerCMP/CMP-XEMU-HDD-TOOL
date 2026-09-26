#!/usr/bin/env python3
import argparse
import hashlib
from pathlib import Path
import subprocess
import tempfile
from integration import make_fixture
ap=argparse.ArgumentParser();ap.add_argument('--binary',type=Path,required=True);args=ap.parse_args()
with tempfile.TemporaryDirectory(prefix='xhc-faults-') as t:
    root=Path(t);source=root/'source.raw';make_fixture(source)
    with source.open('rb') as f:before=hashlib.file_digest(f,'sha256').hexdigest()
    subprocess.run([str(args.binary),str(source),str(root)],check=True,timeout=120)
    with source.open('rb') as f:after=hashlib.file_digest(f,'sha256').hexdigest()
    assert before==after
    print('PASS: complete source content unchanged after corruption/cancellation/publication-race tests')
