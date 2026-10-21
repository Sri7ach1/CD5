# Corresponding source delivery — 0.9.1-beta

The binary release must be accompanied by the generated source archive. Nothing
has been published by this audit. The archive contains current CD5 sources and
assets, the helper, notices and build/install scripts, plus `dependency-sources/`:

- Complete SDK source at f5d40f02770e9b0001dd9e18056eca70b3da8b40 (including CRT,
  kernel support, libc, stubs and headers).
- Complete curl 8.18.0 release and Mbed TLS 3.6.0 tag source.
- Exact Mozilla certdata.txt and unmodified curl mk-ca-bundle.pl 1.33 converter.
- Installed libc++ source headers with generated target configuration.
- PS5SX2 native tooling source subset, linker scripts and GPL text at the pinned
  commit. No prebuilt native tool is needed to reconstruct it.

All archive/file hashes and origins are in dependency-sources.json. Third-party
notices remain in their source files and under third_party/licenses. The SDK
notice collection deliberately includes more files than the objects selected
by the CD5 linker, to preserve header and optional helper attributions as well.

## Proven rebuild on the development host

A disposable container from the recorded original SDK image was used as the
compiler/host environment. Its installed target SDK was moved aside. Running
`install_audited_dependencies.sh` created a fresh SDK entirely from the locked
source inputs, without downloading code. All selected archive hashes and the
CA bundle matched the previous installed versions exactly. The derived local
image is `cd5-sdk-audited:0.9.1`. The original image was preserved.

`reproduce_dependencies.sh` is an additional comparison recipe: mount an audit
directory with the locked archives in `sources/` at `/audit` and run it inside
the original SDK image. It rebuilds libc, Mbed TLS and curl, compares archives,
and retains the build/configure logs under `/audit/evidence`.

## Rebuild from the exported source archive

Extract it and enter its CD5 directory. The clean bootstrap Dockerfile installs
host compilation tools from Ubuntu 24.04 repositories; this bootstrap path is
provided for other machines. The actual audited run used the known installed
Ubuntu clang 18.1.3 environment described above. Changes in distro tools may
produce a hash mismatch; the script fails rather than labeling that identical.

```sh
docker build -f tools/audited-sdk.Dockerfile -t cd5-sdk-audited:0.9.1 .
mkdir -p .deps/native-source
tar xf dependency-sources/native-tools.tar -C .deps/native-source
native="$PWD/.deps/native-source/ps5/proto/native"
c++ -std=c++20 -O2 "$native/tooling/native/native_app_builder.cpp" \
    "$native/tooling/native/elf_object.cpp" \
    "$native/tooling/native/sce_module_writer.cpp" \
    "$native/tooling/native/self_container.cpp" -lz \
    -o "$native/tooling/native/ps5-native-tool"
CD5_NATIVE_SDK="$native" CD5_SDK_IMAGE=cd5-sdk-audited:0.9.1 ./scripts/build.sh
CD5_SDK_IMAGE=cd5-sdk-audited:0.9.1 ./scripts/build_unjail.sh
```

The host native-tool build requires a C++20 compiler and zlib development headers.
Building it was checked: it generates exactly the same eboot as the prior tool.
Package using scripts/package_ffpfsc.sh with a new CD5_PACKAGE_OUTPUT; this needs
MkPFS and its build-only Python dependencies, installed by the existing pinned
bootstrap. The helper ELF remains separate and is never run by these commands.

## Prepare the paired local artifacts

After a successful application build and package verification:

```sh
python3 scripts/package_sources.py .deps/license-audit/sources \
    dist/CD5-0.9.1-beta-corresponding-source.tar.gz
```

This command refuses an existing output and validates every external source
hash. The export includes the source tree and build manifest, not the opaque
Docker image, prebuilt SDK libraries, helper ELF or Git history. Keep the source
archive checksum next to the matching FF PFSC checksum. If code/dependencies
change, rebuild and export a new pair; do not reuse the previous archive.
