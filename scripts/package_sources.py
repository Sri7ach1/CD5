#!/usr/bin/env python3
"""Export CD5 plus audited dependency sources; never publish or upload."""
from pathlib import Path
import hashlib, json, subprocess, sys, tarfile
ROOT=Path(__file__).resolve().parents[1]
sources=Path(sys.argv[1]).resolve()
output=Path(sys.argv[2]).resolve()
if output.exists(): raise SystemExit('Output already exists')
subprocess.run([sys.executable,str(ROOT/'scripts/build_manifest.py'),'verify'],check=True)
lock=json.loads((ROOT/'docs/dependency-sources.json').read_text())
for name, item in lock.items():
    if hashlib.sha256((sources/name).read_bytes()).hexdigest()!=item['sha256']:
        raise SystemExit('Source hash mismatch: '+name)
git=['git','-c','safe.directory='+str(ROOT)]
files=subprocess.check_output(git+['ls-files','--cached','--others','--exclude-standard','-z'],cwd=ROOT).decode().split('\0')
manifest=json.loads((ROOT/'build/build-manifest.json').read_text())
missing=set(manifest['source_files'])-set(files)
if missing: raise SystemExit('Build inputs missing from export: '+', '.join(sorted(missing)))
with tarfile.open(output,'w:gz') as tar:
    for name in sorted(set(files)-{''}):
        p=ROOT/name
        if p.is_file(): tar.add(p,arcname='CD5/'+name,recursive=False)
    tar.add(ROOT/'build/build-manifest.json',arcname='CD5/build-record/build-manifest.json',recursive=False)
    for name in lock: tar.add(sources/name,arcname='CD5/dependency-sources/'+name,recursive=False)
print(hashlib.sha256(output.read_bytes()).hexdigest()+'  '+str(output))
