# CD5

**Native Audio CD Player for PlayStation 5**

**CD5** is a native PlayStation 5 homebrew application that plays physical Audio CDs through a compatible USB optical drive.

Developed by **[Sri7ach1](https://github.com/Sri7ach1)**.

**Published version:** `0.9.0-beta`

**Candidate on this branch:** `0.9.1-beta` (PS5 validation pending)

**Repository:** https://github.com/Sri7ach1/CD5

---

## Overview

CD5 brings physical Audio CD playback to the PlayStation 5 homebrew environment.

It combines direct CD Digital Audio (CDDA) playback with online album identification, cover artwork, track information and synchronized lyrics in a modern interface.

CD5 runs locally on the PS5. A PC is not required for normal playback after installation and setup.

## Features

- Native PlayStation 5 application
- Physical Audio CD playback using a USB optical drive
- CDDA digital audio extraction
- Stereo audio output at 48 kHz
- Resampling from CDDA's native 44.1 kHz
- DualSense playback controls
- Automatic disc detection
- Disc removal and reinsertion support
- MusicBrainz Disc ID identification
- Album, artist and track metadata
- Cover Art Archive artwork
- CD5-branded fallback artwork
- Lyrics retrieval through LRCLIB
- Synchronized LRC lyrics with automatic scrolling
- Karaoke-style line highlighting
- Modern three-column interface
- Ubuntu UI typography and Bebas Neue branding
- Purple accent color based on `#974FE0`
- No persistent cache for downloaded metadata, artwork or lyrics

## Interface

CD5 uses three main areas:

**Left — Player**
- Album artwork
- Current song, artist and album
- Playback progress and duration
- Playback controls

**Center — Lyrics**
- Synchronized lyrics when available
- Automatic scrolling
- Active-line highlighting
- Plain-lyrics fallback when timestamps are unavailable

**Right — Tracks**
- Disc track listing
- Track durations
- Current-track indication

## Controls

| DualSense input | Action |
|---|---|
| Cross (X) | Play / Pause |
| Square | Stop |
| D-pad Left | Previous track |
| D-pad Right | Next track |
| OPTIONS | Reload disc information and play from track 1 |

When stopped, Left and Right select a track without starting playback. Cross starts playback from the selected track.

OPTIONS also retries metadata and artwork requests. It restarts playback from
track 1 even when paused or stopped. With no playable disc, Square closes CD5.

## Tested Hardware and Firmware

CD5 has been tested successfully with the following configuration:

| Component | Tested configuration |
|---|---|
| Console | PlayStation 5 Slim |
| System firmware | **13.60** |
| Homebrew / jailbreak environment | **Relapse** |
| Optical drive | **LiteON eBAU102-21** external USB CD/DVD drive |
| Media | Physical Audio CDs |
| Application version | **0.9.0-beta** |

The following features have been tested on this setup:

- Physical Audio CD playback
- Track selection, play, pause and stop
- MusicBrainz album and track identification
- Cover Art Archive artwork
- LRCLIB lyrics and synchronized karaoke display
- Disc ejection and reinsertion during an application session

**Compatibility notice:** This is the configuration verified by the developer.
Support for other PS5 firmware versions, jailbreak environments and USB
optical drives has not yet been confirmed. Other configurations may work,
but they should not be considered officially tested.

## Requirements

- A PlayStation 5 with a compatible homebrew execution environment
- A compatible USB optical CD drive
- A physical Audio CD
- Required homebrew payload services for device access
- Network connectivity for online metadata, artwork and lyrics

Online services are optional for basic CDDA playback.

CD5 is intended for compatible homebrew-enabled PS5 environments. It is not an official Sony application.

## Installation

CD5 is distributed in the `.ffpfsc` homebrew format used by the target environment.

1. Set up a compatible PS5 homebrew environment.
2. Install or mount the CD5 `.ffpfsc` application using the supported homebrew workflow.
3. Ensure the required local payload services are available.
4. Connect a compatible USB CD drive.
5. Launch CD5 from the PS5.
6. Insert an Audio CD.

The application should detect the disc and begin playback automatically.

A computer is not required for day-to-day operation after setup.

## Online services

CD5 uses these external services:

- [MusicBrainz](https://musicbrainz.org/) — disc identification and metadata
- [Cover Art Archive](https://coverartarchive.org/) — album artwork
- [LRCLIB](https://lrclib.net/) — lyrics and synchronized LRC data

Availability and accuracy depend on the corresponding services and their databases.

Some discs may not be identified correctly, and some tracks may not have cover artwork or synchronized lyrics.

Downloaded metadata, artwork and lyrics are held in memory. CD5 does not intentionally persist these resources as a local cache.

## Building

This repository contains the native application source code and build scripts.

The development environment currently uses Ubuntu, Docker and the PS5 Payload SDK/toolchain.

The build is environment-specific and may require additional runtime/toolchain components.

From the project root:

```bash
./scripts/build.sh
./scripts/package_ffpfsc.sh
```

The packaged application is generated at:

`dist/CD5.ffpfsc`

See [build and installation](docs/build-and-install.md). Set `CD5_NATIVE_SDK` explicitly before building. Existing package outputs are never overwritten; use `CD5_PACKAGE_OUTPUT` for a new candidate filename.

## Native Build Dependencies

Building CD5 currently requires additional native PS5 development
components that are not included in this repository:

- PS5 Payload SDK and its Docker development environment
- Clang 18 and the native compilation wrapper (`prospero-clang18`)
- Native PS5 linker script (`ps5-pie.ld`)
- Native application symbol map (`app-symbols.map`)
- Native SELF conversion/signing tool (`ps5-native-tool`)
- Startup source `runtime/app_crt.cpp` (included; compiled by the build script)
- MkPFS for generating the `.ffpfsc` distribution

The native toolchain location can be configured with:

    CD5_NATIVE_SDK=/path/to/native ./scripts/build.sh

The Docker image can be selected with `CD5_SDK_IMAGE`.

The startup source is included with verified correspondence to the former
runtime object. The SDK and native tools still need to be provisioned.
See [build and installation](docs/build-and-install.md) for explicit
configuration and helper requirements, and [dependency audit](docs/dependency-audit.md)
for remaining distribution questions.

## Current Status

**Beta development — not yet a stable release.**

Core playback, online metadata, artwork, synchronized lyrics and disc reinsertion are implemented.

Remaining work includes final rendering optimizations, reliability testing, compatibility checks, dependency/license review and release packaging.

## Origins and Acknowledgements

### PS5SX2

Original project: https://github.com/Swordpdf/PS5SX2

Original project license: GPL-3.0-or-later.

CD5 was developed using **PS5SX2 (PlayStation 2 emulator for PlayStation 5)**
as an important technical foundation and development reference.

PS5SX2 helped provide the native PS5 development environment, tooling
and technical basis used during the creation of CD5.

CD5 is a separate project focused on native Audio CD playback.
It is not a PS2 emulator and is not an official PS5SX2 release.

**Special thanks to the PS5SX2 project and its contributors** for their
work on the PlayStation 5 homebrew ecosystem.

Any PS5SX2-derived components included in CD5 must retain their
applicable original copyright notices and license conditions.

## Credits

**CD5** is created and developed by **Sri7ach1**.

- Author: https://github.com/Sri7ach1
- Project: https://github.com/Sri7ach1/CD5

Third-party components and online services retain their respective copyrights, trademarks and licenses.

CD5 is an independent homebrew project and is not affiliated with, endorsed by or sponsored by Sony Interactive Entertainment.

## License

CD5 is distributed under the **GNU General Public License,
version 3 or later (GPL-3.0-or-later)**, subject to applicable
third-party license conditions.

Copyright (c) 2026 Sri7ach1.

The full license text is available in [LICENSE](LICENSE).

Forks, modifications and contributions are welcome.

Anyone redistributing GPL-covered versions of CD5 must comply
with the GPL's copyright, licensing and source-code requirements.

Third-party components retain their original copyright notices
and licenses.

See [THIRD_PARTY_NOTICES.md](THIRD_PARTY_NOTICES.md).

## Contributing

**Forks, improvements and pull requests are welcome!**

Whether you want to improve performance, add support for more USB drives,
fix bugs or enhance the user interface, contributions are appreciated.

See [CONTRIBUTING.md](CONTRIBUTING.md) to get started.

---

**CD5 — Physical music, native playback.**

## 0.9.1-beta development candidate

Unreleased and pending PS5 validation. Changes include recoverable CD read
errors, optical discovery/reconnection, session-aware audio bounds, synchronized
metadata snapshots and bounded network retries. OPTIONS reloads disc information
and restarts playback. AudioOut failure exits cleanly and requires relaunch.

[Validation and known limits](docs/0.9.1-validation.md) ·
[Release notes](RELEASE_NOTES.md) · [Dependency audit](docs/dependency-audit.md).
