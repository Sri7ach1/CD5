# Dependency audit — CD5 0.9.1-beta

Updated 2026-10-20. CD5 and unjail helper author: Sri7ach1.
**Technical source/provenance and notice audit completed for this candidate.**
The previous unresolved correspondence items below have been resolved with
recompilation and source delivery, not merely matching version labels.
This does not constitute a warranty or a hardware compatibility certification.

## Evidence and resolution

The original SDK image is
`sha256:d01c3865e0e5f57ff27c9f9649af068aa0554ae1f7fcf8090df96693f7c2918c`;
compiler: Ubuntu clang 18.1.3. Its clean SDK source checkout is
`f5d40f02770e9b0001dd9e18056eca70b3da8b40`.
[Reproduction results](audit-reproduction.json) record archive hashes and selected
link-map members. [Source lock](dependency-sources.json) records every supplied
external source input, origin and SHA256. [Rebuild instructions](source-delivery.md)
explain the offline dependency build and the companion source archive.

| Component | Finding and resolution |
| --- | --- |
| Native startup | Unmodified BlackBearReloaded GPL-3.0-or-later source from PS5SX2 b4cf55e87c34e8a2a1cd9302df4373e5a94d28f4. Rebuilt object identical to the old external app_crt.o; source now included. |
| curl 8.18.0 | Original Docker build recipe recovered. Official release source plus documented tool_xattr.h edit rebuilt to an identical libcurl.a. Full release source and COPYING supplied. |
| Mbed TLS 3.6.0 | Official tag, default configuration and -Os SDK build reproduced all three installed archives exactly. Full source and Apache-2.0 OR GPL-2.0-or-later license supplied; CD5 uses the Apache-2.0 option. |
| SDK libc | Complete pinned SDK source rebuilt to identical libc.a. Selected objects include GPLv3+, BSD, MIT and LLVM-licensed code. Per-file notices preserved in PS5-SDK-NOTICES.txt and complete sources supplied. |
| pthread | SDK libpthread.a is an empty archive, not a statically embedded implementation. Platform symbols are resolved through the SDK import stubs; SDK sources include the stubs and headers. |
| libc++ / libc++abi / libunwind | Original map selects no objects from these archives. Removed unused archive link arguments. Installed libc++ source headers and generated configuration are supplied for app_crt.cpp compilation, with LLVM license texts. |
| CA bundle | Located exact Mozilla certdata.txt at f6dd0159931e536991eaaa02d839bdffb92c3f22. Its hash matches the bundle header. Upstream mk-ca-bundle.pl 1.33 at 0ada20387c31c638cfd7f6b4ae7e5cab5b318caf reproduces the complete bundle byte-for-byte when conversion time is fixed to 2026-09-25 03:12:01 UTC. Both sources, MPL-2.0 and attribution supplied. |
| Native tool | Pinned PS5SX2 source subset and linker files supplied. Rebuilt host converter produces exactly the same eboot.bin from the existing linked ELF. Host C++ compiler and zlib are build tools, not embedded PS5 libraries. |
| stb / jsmn | Source headers and MIT notices retained. |
| Ubuntu / Bebas Neue | Font files and Ubuntu Font Licence / SIL OFL retained; no font modifications. |
| Branding | Existing CD5 artwork retained, attributed to the CD5 project; no new third-party artwork introduced. |
| unjail_pid | Author explicitly confirmed ownership and GPL-3.0-or-later authorization on 2026-10-20. Licensed source incorporated, exact target-path selection fixed, and separately compiled. SDK kernel implementation source included. No third-party permission remains pending for this helper on that evidence. |
| MkPFS | Build-only tool pinned by the existing setup script, GPLv3. Not linked into CD5. Its source origin/version remains documented by that script. |

## What accompanies a binary release

Distribute the complete `CD5-0.9.1-beta-corresponding-source.tar.gz` beside the
matching binary, with its checksum. It includes CD5, assets, helper source,
SDK source, curl, Mbed TLS, certificate source/converter, libc++ source headers,
native tools and rebuild scripts. A patch alone is not this source delivery.
Retain the license/notice files in the package and source archive. Do not replace
the supplied source archive with links to moving branches or an opaque SDK image.

The optional helper ELF must be accompanied by the same source bundle if
released separately. No payload loader, proprietary console firmware or Sony
library binary is bundled. Public redistribution remains an explicit maintainer
action; this work does not upload anything. Actual publication/source availability
has not occurred and must not be claimed as already satisfied.

## Helper architecture

Payload Manager executes the helper in a context initialized by a compatible
payload loader. SDK crt/kernel.c::__kernel_init consumes socket/pipe descriptors
and kernel addresses from payload_args_t. CD5's native app_crt.cpp initializes an
application runtime and receives no equivalent kernel context. Merely linking
the helper into CD5 does not supply the privilege prerequisite.

A compatible alternate payload loader can replace Payload Manager in principle:
start CD5, then execute the helper with the SDK context. This needs console testing.
CD5 continues to drive discovery if its automatic HTTP helper request fails.
The source correction replaces the old broad UID/newest-eboot selection; the old
helper did **not** actually use its declared target path. See helpers/unjail_pid.

## Primary references

- GPL source delivery: https://www.gnu.org/licenses/gpl-3.0.html#section6
- Static linking/source FAQ: https://www.gnu.org/licenses/gpl-faq.html#GPLStaticVsDynamic
- SDK: https://github.com/ps5-payload-dev/sdk/tree/f5d40f02770e9b0001dd9e18056eca70b3da8b40
- curl: https://curl.se/download/curl-8.18.0.tar.xz
- Mbed TLS: https://github.com/Mbed-TLS/mbedtls/tree/v3.6.0
- Exact remaining source URLs and hashes: dependency-sources.json.
