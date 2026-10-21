#!/bin/bash
set -euo pipefail
export PATH=/opt/ps5-payload-sdk/bin:$PATH
export CC=prospero-clang CXX=prospero-clang++ AR=prospero-ar NM=prospero-nm RANLIB=prospero-ranlib
mkdir -p /audit/evidence /audit/rebuild/sdk
cd /audit/rebuild/sdk
tar xf /audit/sources/sdk-f5d40f0.tar
make -C libc -j4 > /audit/evidence/libc-build.log 2>&1
cmp libc/libc.a /opt/ps5-payload-sdk/target/lib/libc.a
sha256sum libc/libc.a
cd /audit/rebuild
tar xf /audit/sources/mbedtls-3.6.0.tar.gz
cd mbedtls-3.6.0
make CC=prospero-clang AR=prospero-ar RANLIB=prospero-ranlib CFLAGS=-Os lib -j4 > /audit/evidence/mbedtls-build.log 2>&1
for lib in libmbedcrypto.a libmbedtls.a libmbedx509.a; do cmp library/$lib /opt/ps5-payload-sdk/target/lib/$lib; done
sha256sum library/*.a
cd /audit/rebuild
tar xf /audit/sources/curl-8.18.0.tar.xz
cd curl-8.18.0
sed -i 's|define USE_XATTR| |g' src/tool_xattr.h
./configure --prefix=/opt/ps5-payload-sdk/target --host=x86_64-pc-freebsd --enable-static --disable-shared --with-mbedtls=/opt/ps5-payload-sdk/target --without-openssl --without-libpsl --disable-docs --disable-ftp --disable-ldap --disable-ldaps --disable-rtsp --disable-proxy --disable-dict --disable-telnet --disable-tftp --disable-pop3 --disable-imap --disable-smb --disable-smtp --disable-gopher --disable-mqtt > /audit/evidence/curl-configure.log 2>&1
make -j4 > /audit/evidence/curl-build.log 2>&1
cmp lib/.libs/libcurl.a /opt/ps5-payload-sdk/target/lib/libcurl.a
sha256sum lib/.libs/libcurl.a
