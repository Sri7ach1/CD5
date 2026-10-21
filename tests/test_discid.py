#!/usr/bin/env python3
"""Run production Disc ID code with hashlib backing the SHA-1 API on the host."""
from pathlib import Path
import base64
import ctypes as C
import hashlib
import subprocess
import tempfile
r=Path(__file__).resolve().parents[1]
class Toc(C.Structure):
    _fields_=[('first_track',C.c_uint8),('last_track',C.c_uint8),('frame_offset',C.c_uint32*100)]
with tempfile.TemporaryDirectory() as temp:
    p=Path(temp);(p/'mbedtls').mkdir()
    (p/'mbedtls/sha1.h').write_text('#include <stddef.h>\nint mbedtls_sha1(const unsigned char *, size_t, unsigned char *);\n')
    (p/'shim.c').write_text('''#include <stddef.h>
    int (*sha_callback)(const unsigned char *, size_t, unsigned char *);
    void set_sha(int (*fn)(const unsigned char *, size_t, unsigned char *)) {sha_callback=fn;}
    int mbedtls_sha1(const unsigned char *s,size_t n,unsigned char *d) {return sha_callback(s,n,d);}
    ''')
    subprocess.run(['cc','-shared','-fPIC','-Wall','-Wextra','-Werror','-I',str(p),str(r/'src/discid.c'),str(p/'shim.c'),'-o',str(p/'test.so')],check=True)
    lib=C.CDLL(str(p/'test.so'))
    callback=C.CFUNCTYPE(C.c_int,C.c_void_p,C.c_size_t,C.c_void_p)
    @callback
    def sha(data,n,out):
        if n != 804: return -1
        C.memmove(out,hashlib.sha1(C.string_at(data,n)).digest(),20);return 0
    lib.set_sha.argtypes=[callback];lib.set_sha(sha)
    lib.cd5_discid_generate.argtypes=[C.POINTER(Toc),C.c_void_p]
    lib.cd5_discid_make_toc_query.argtypes=[C.POINTER(Toc),C.c_void_p,C.c_size_t]
    toc=Toc(1,2);toc.frame_offset[0]=20150;toc.frame_offset[1]=150;toc.frame_offset[2]=10150
    out=C.create_string_buffer(29);query=C.create_string_buffer(1024)
    assert lib.cd5_discid_generate(C.byref(toc),out)==1
    preimage='0102'+''.join(f'{x:08X}' for x in toc.frame_offset)
    expected=base64.b64encode(hashlib.sha1(preimage.encode()).digest()).decode().translate(str.maketrans('+/=','._-'))
    assert out.value.decode()==expected
    assert lib.cd5_discid_make_toc_query(C.byref(toc),query,1024)==1
    assert query.value==b'1+2+20150+150+10150'
    toc.first_track=2;toc.frame_offset[1]=0
    assert lib.cd5_discid_make_toc_query(C.byref(toc),query,1024)==1
    assert query.value==b'2+2+20150+10150' # last track number, not count
    assert lib.cd5_discid_make_toc_query(C.byref(toc),query,2)==0
    toc.frame_offset[2]=20150
    assert lib.cd5_discid_generate(C.byref(toc),out)==0
    toc.last_track=100
    assert lib.cd5_discid_make_toc_query(C.byref(toc),query,1024)==0
print('PASS: Disc ID SHA-1/base64, TOC query numbering, invalid offsets and buffer limits')
