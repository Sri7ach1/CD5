#!/usr/bin/env python3
from pathlib import Path
import tempfile, shutil, subprocess
root=Path(__file__).resolve().parents[1]
with tempfile.TemporaryDirectory() as temp:
    p=Path(temp);(p/'scripts').mkdir();(p/'src').mkdir();(p/'package/PPSA00001').mkdir(parents=True)
    shutil.copyfile(root/'scripts/build_manifest.py',p/'scripts/build_manifest.py')
    source=p/'src/test.c';source.write_text('original')
    binary=p/'package/PPSA00001/eboot.bin';binary.write_text('binary')
    def run(mode,success):
        result=subprocess.run(['python3',str(p/'scripts/build_manifest.py'),mode],capture_output=True)
        assert (result.returncode==0)==success,(mode,result.stderr)
    run('begin',True);run('verify',False);run('finish',True);run('verify',True)
    (p/'src/test.c.pre_old').write_text('historical backup, not a build input')
    run('verify',True)
    source.write_text('changed');run('verify',False)
    run('begin',True);run('finish',True);binary.write_text('tampered');run('verify',False)
    run('begin',True);source.write_text('changed while building');run('finish',False)
print('PASS: package manifest rejects incomplete, stale and modified builds')
