#!/usr/bin/env bash
# Run only inside a disposable SDK build container. No network downloads.
set -euo pipefail
ROOT="$(cd "$(dirname "${BASH_SOURCE[0]}")/.." && pwd)"
SOURCE_DIR="${1:?Pass the directory containing the locked source archives}"
python3 - "$ROOT" "$SOURCE_DIR" <<'PY'
from pathlib import Path
import sys,json,hashlib
root,source=map(Path,sys.argv[1:])
for name,item in json.loads((root/'docs/dependency-sources.json').read_text()).items():
    if hashlib.sha256((source/name).read_bytes()).hexdigest()!=item['sha256']:
        sys.exit('Source hash mismatch: '+name)
PY
BUILD_ROOT="$(mktemp -d)"
trap 'rm -rf -- "$BUILD_ROOT"' EXIT
mkdir "$BUILD_ROOT/sdk"
tar xf "$SOURCE_DIR/sdk-f5d40f0.tar" -C "$BUILD_ROOT/sdk"
make -C "$BUILD_ROOT/sdk" -j4 DESTDIR=/opt/ps5-payload-sdk install
# Source headers include the generated platform configuration. No target C++
# runtime archive is needed by CD5; none was selected in the original link map.
tar xf "$SOURCE_DIR/libcxx-headers.tar" -C /opt/ps5-payload-sdk/target/include
export PATH=/opt/ps5-payload-sdk/bin:$PATH
export CC=prospero-clang CXX=prospero-clang++ AR=prospero-ar NM=prospero-nm RANLIB=prospero-ranlib
cd "$BUILD_ROOT"
tar xf "$SOURCE_DIR/mbedtls-3.6.0.tar.gz"
cd mbedtls-3.6.0
make CC=prospero-clang AR=prospero-ar RANLIB=prospero-ranlib CFLAGS=-Os lib -j4
cp -r include/mbedtls include/psa /opt/ps5-payload-sdk/target/include/
cp library/libmbedtls.a library/libmbedx509.a library/libmbedcrypto.a /opt/ps5-payload-sdk/target/lib/
cd "$BUILD_ROOT"
tar xf "$SOURCE_DIR/curl-8.18.0.tar.xz"
cd curl-8.18.0
sed -i 's|define USE_XATTR| |g' src/tool_xattr.h
./configure --prefix=/opt/ps5-payload-sdk/target --host=x86_64-pc-freebsd --enable-static --disable-shared --with-mbedtls=/opt/ps5-payload-sdk/target --without-openssl --without-libpsl --disable-docs --disable-ftp --disable-ldap --disable-ldaps --disable-rtsp --disable-proxy --disable-dict --disable-telnet --disable-tftp --disable-pop3 --disable-imap --disable-smb --disable-smtp --disable-gopher --disable-mqtt
make -j4
make install
cd "$BUILD_ROOT"
cp "$SOURCE_DIR/certdata.txt" "$SOURCE_DIR/mk-ca-bundle.pl" .
python3 - <<'PY'
import os,datetime
stamp=int(datetime.datetime(2026,9,25,3,12,1,tzinfo=datetime.timezone.utc).timestamp())
os.utime('certdata.txt',(stamp,stamp))
print(stamp)
PY
# Freeze conversion time to the date of the audited snapshot: certificate
# expiry decisions are time-dependent. Preserve the upstream converter intact.
perl -e 'BEGIN { *CORE::GLOBAL::time = sub () { 1790305921 } } do "./mk-ca-bundle.pl"; die $@ if $@;' -- -n -f ca-bundle.crt
mkdir -p /opt/ps5-payload-sdk/target/etc
cp ca-bundle.crt /opt/ps5-payload-sdk/target/etc/ca-bundle.crt
python3 - "$ROOT" <<'PY'
from pathlib import Path
import json,hashlib,sys
root=Path(sys.argv[1]);sdk=Path('/opt/ps5-payload-sdk')
inv=json.loads((root/'docs/sdk-inventory.json').read_text())
for name,item in inv['files'].items():
    if Path(name).name in ('libc++.a','libc++abi.a','libunwind.a'):continue
    actual=hashlib.sha256((sdk/name).read_bytes()).hexdigest()
    if actual!=item['sha256']:sys.exit('Reproduction mismatch: '+name)
    print('MATCH',name,actual)
PY
