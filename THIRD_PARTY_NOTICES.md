# Third-Party Notices

CD5 is an independent, open-source homebrew project developed by Sri7ach1.

The original CD5 source code is distributed under GPL-3.0-or-later.
See [LICENSE](LICENSE).

Third-party components retain their respective copyrights and licenses.
The CD5 GPL license does not replace or override those licenses.

## Included components

### stb

Components:
- `third_party/stb/stb_image.h`
- `third_party/stb/stb_truetype.h`

License information:
- [STB-LICENSE](third_party/licenses/STB-LICENSE)

Project:
https://github.com/nothings/stb

### jsmn

Component:
- `third_party/jsmn/jsmn.h`

License:
- [jsmn/LICENSE](third_party/jsmn/LICENSE)

Project:
https://github.com/zserge/jsmn

### Ubuntu Font Family

Files:
- `assets/fonts/Ubuntu-Regular.ttf`
- `assets/fonts/Ubuntu-Bold.ttf`

License information:
- [Ubuntu-LICENCE.txt](third_party/licenses/Ubuntu-LICENCE.txt)

### Bebas Neue

File:
- `assets/fonts/BebasNeue-Regular.ttf`

License information:
- [BebasNeue-OFL.txt](third_party/licenses/BebasNeue-OFL.txt)

## PS5SX2

Project: https://github.com/Swordpdf/PS5SX2

License: GPL-3.0-or-later, with separately licensed third-party components.

CD5's development environment and native PS5 build workflow were
based in part on the PS5SX2 PlayStation 2 emulator project.

Credit for the original PS5SX2 work belongs to its respective
authors and contributors.

PS5SX2-derived code or tools, where used, remain subject to their
original licenses and copyright notices.

The CD5 GPL license does not supersede any applicable PS5SX2 licenses.

Before redistributing any adapted PS5SX2 source, runtime objects,
or native development tools, their origin and licensing must be verified.

## External build and runtime dependencies

CD5 uses additional components provided by its PS5 development environment,
including the PS5 Payload SDK, native build tools, and linked libraries
such as libcurl and Mbed TLS.

Those components are governed by their own licenses and distribution terms.

The startup source `runtime/app_crt.cpp` is included, copyright 2026
BlackBearReloaded, GPL-3.0-or-later. It is copied unmodified from the pinned
PS5SX2 revision documented in `runtime/README.md`. Rebuilding it reproduces
the former external object byte-for-byte.

The native build tools and the original PS5SX2 development tree are not
vendored into this repository.

## Online services

CD5 communicates with the following third-party services:

- MusicBrainz: https://musicbrainz.org/
- Cover Art Archive: https://coverartarchive.org/
- LRCLIB: https://lrclib.net/

These services are independent of CD5 and have their own terms, data
licenses, and availability policies.

## Redistribution review

Before distributing binary releases, the project must verify applicable
notices and redistribution obligations for all bundled and statically
linked components, including native runtime libraries and certificate data.

The completed candidate audit and source-delivery requirements are recorded below.

## Trademarks

PlayStation and related marks belong to their respective owners.

CD5 is not affiliated with, endorsed by, or sponsored by Sony Interactive
Entertainment.

## 0.9.1 dependency audit completed

See [docs/dependency-audit.md](docs/dependency-audit.md) for source correspondence,
reproduction evidence and release source-delivery instructions. The companion
corresponding-source archive must accompany the matching binary release.

- curl 8.18.0: `third_party/licenses/CURL-COPYING.txt`.
- Mbed TLS 3.6.0: `MBEDTLS-LICENSE.txt`; Apache-2.0 option selected.
- SDK libc and headers: `PS5-SDK-NOTICES.txt`, with full per-file source notices.
  GPL-3.0-or-later portions retain John Törnblom's attribution; BSD, MIT and LLVM
  portions retain their own terms.
- LLVM source headers and emulated TLS: `LLVM-LICENSE.txt` and runtime license
  texts. No libc++, libc++abi or libunwind archive objects enter CD5.
- Mozilla NSS certificate data: `MPL-2.0.txt` and `MOZILLA-CA-NOTICE.txt`.
  Unmodified certdata.txt and the original curl converter accompany the source.
- `helpers/unjail_pid/unjail_pid.c`: copyright 2026 Sri7ach1,
  GPL-3.0-or-later, licensed with the author's explicit authorization.

All referenced standalone notice files are under `third_party/licenses/` and
are copied into the generated application package. No proprietary console
runtime libraries or external payload-manager binary are included.
