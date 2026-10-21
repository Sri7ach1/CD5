#!/usr/bin/env python3
"""Read-only inventory; run inside the selected SDK image with CD5 mounted."""
import hashlib
import json
from pathlib import Path
import re
import subprocess
sdk = Path('/opt/ps5-payload-sdk')
source = Path('/opt/ps5-payload-sdk-source')
def command(*args):
    result = subprocess.run(args, capture_output=True, text=True)
    return result.stdout.strip() if result.returncode == 0 else None
def digest(path):
    return hashlib.sha256(path.read_bytes()).hexdigest()
def version(path, macro):
    text = path.read_text()
    match = re.search(r'^#define\s+' + macro + r'\s+(.+)$', text, re.M)
    return match[1] if match else None
files = ['target/lib/' + n for n in ['libcurl.a', 'libmbedtls.a', 'libmbedx509.a', 'libmbedcrypto.a', 'libc.a', 'libc++.a', 'libc++abi.a', 'libunwind.a', 'libpthread.a']]
files += ['target/etc/ca-bundle.crt']
print(json.dumps({
    'scope': 'SDK inputs; hashes do not establish exact upstream source correspondence',
    'compiler': command('clang-18','--version'),
    'sdk_source_commit': command('git','-C',str(source),'rev-parse','HEAD'),
    'sdk_source_changes': command('git','-C',str(source),'status','--porcelain'),
    'curl_version': version(sdk/'target/include/curl/curlver.h','LIBCURL_VERSION'),
    'mbedtls_version': version(sdk/'target/include/mbedtls/build_info.h','MBEDTLS_VERSION_STRING'),
    'files': {name: {'sha256':digest(sdk/name), 'bytes':(sdk/name).stat().st_size} for name in files if (sdk/name).is_file()},
    'ca_header': (sdk/'target/etc/ca-bundle.crt').read_text().splitlines()[:20],
}, indent=2))
