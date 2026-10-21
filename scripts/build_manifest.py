#!/usr/bin/env python3
"""Refuse packaging after a failed build or changed source/package inputs."""
from pathlib import Path
import fnmatch
import hashlib
import json
import os
import sys
ROOT = Path(__file__).resolve().parents[1]
MANIFEST = ROOT / 'build/build-manifest.json'
def sha(path):
    return hashlib.sha256(path.read_bytes()).hexdigest()
def inputs():
    paths=[]
    for directory in ('src','runtime','assets','third_party','scripts','helpers','tools'):
        paths += [p for p in (ROOT/directory).rglob('*') if p.is_file() and
                  p.suffix not in ('.o','.pyc','.tmp') and '__pycache__' not in p.parts and
                  not any(fnmatch.fnmatch(p.name, pattern) for pattern in
                          ('*.pre_*','*.pre-*','*.bak','*.backup')) and
                  p != ROOT/'tools/make_fself.py']
    paths += [ROOT/n for n in ('LICENSE','THIRD_PARTY_NOTICES.md','docs/dependency-audit.md','docs/dependency-sources.json','docs/source-delivery.md') if (ROOT/n).is_file()]
    return {str(p.relative_to(ROOT)):sha(p) for p in sorted(paths)}
def package():
    root=ROOT/'package/PPSA00001'
    return {str(p.relative_to(ROOT)):sha(p) for p in sorted(root.rglob('*')) if p.is_file()}
def main():
    mode=sys.argv[1]
    if mode=='begin':
        data={'complete':False,'source_files':inputs(),'sdk_image':os.environ.get('CD5_BUILD_IMAGE_ID','unknown')}
    else:
        data=json.loads(MANIFEST.read_text())
        if data['source_files'] != inputs():
            raise ValueError('Build inputs changed; rebuild before packaging')
        if mode=='finish':
            data.update(complete=True,package_files=package())
            if 'package/PPSA00001/eboot.bin' not in data['package_files']:
                raise ValueError('Missing eboot.bin')
        elif mode=='verify':
            if not data['complete'] or data['package_files'] != package():
                raise ValueError('Build incomplete or package files changed; rebuild')
            print('Build manifest verified')
            return
        else:
            raise ValueError('Expected begin, finish or verify')
    MANIFEST.parent.mkdir(parents=True,exist_ok=True)
    MANIFEST.write_text(json.dumps(data,indent=2)+'\n')
if __name__=='__main__':
    try:
        main()
    except (OSError, ValueError, KeyError, IndexError) as error:
        sys.exit('ERROR: '+str(error))
