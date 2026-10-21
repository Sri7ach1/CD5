# CD5 v0.9.0-beta

First public beta of CD5, a native Audio CD player for PlayStation 5.

Developed by Sri7ach1.

## Features

- Native Audio CD playback from a USB optical drive
- Stereo CDDA playback with 44.1 kHz to 48 kHz resampling
- Play, pause, stop and track navigation
- Automatic disc detection and disc reinsertion support
- MusicBrainz album and track identification
- Album artwork from Cover Art Archive
- Synchronized karaoke lyrics from LRCLIB
- Three-column interface with a 60 FPS rendering target
- On-device playback without a PC during normal use

## Verified configuration

- Console: PlayStation 5 Slim
- Firmware: 13.60
- Jailbreak environment: Relapse
- USB optical drive: LiteON eBAU102-21

Other firmware versions, jailbreak environments and optical drives
have not yet been verified.

## Known limitations

- Album identification and artwork may occasionally fail to load.
- Some tracks may not have synchronized lyrics.
- Online features require an internet connection.
- Compatibility with other USB optical drives is not guaranteed.
- This is beta software; unexpected behavior may occur.

## Installation

The installable FFPSC package will be provided after the binary
redistribution and license review is completed.

CD5 requires a compatible homebrew-enabled PS5 environment and
the necessary local payload services.

## Source code and credits

CD5: https://github.com/Sri7ach1/CD5

Author: Sri7ach1

License: GPL-3.0-or-later, with applicable third-party licenses.

Technical foundation and acknowledgements:
PS5SX2 by Swordpdf and contributors.
https://github.com/Swordpdf/PS5SX2

Forks, improvements and pull requests are welcome!

## 0.9.1-beta — Unreleased candidate

Hardware validation is pending. The technical dependency audit is completed;
a public release must include the paired corresponding-source archive.

- Replace permanent CD/AudioOut failure spin loops with STOP/manual retry or
  notified process termination. Bound transient READ CD retries to three attempts.
- Discover optical CAM devices through INQUIRY, prefer ready audio media, and
  reopen devices after removal. Keep the UI visible without a reader/disc.
- Validate basic/full TOCs, exclude data tracks, use real session lead-outs, and
  request CD-DA-only sectors. Reject unverifiable mixed-disc layouts safely.
- Derive MusicBrainz Disc ID from the validated TOC; fix last-track query numbering
  and avoid using a medium with the wrong track count.
- Publish metadata/track snapshots under a mutex; reject stale results after a
  readiness/TOC recheck. Retain lyrics generation protection.
- Bound HTTP retries and response/image sizes; enforce HTTPS-only transfers and
  redirects, retain TLS verification, and centralize User-Agent/version/contact.
- OPTIONS retries disc information; distinguish absent and invalid results.
- Build the attributed startup runtime from supplied source with byte-identical
  correspondence to the old external object. Expand dependency notices/inventory.
- Make native SDK/console configuration explicit, remove the launcher's external
  source edits, stop builds on intermediate errors, and verify a build manifest
  before packaging. Preserve existing package outputs.

No audio-buffer/thread architecture rewrite or persistent cache is included.
Readers without a usable raw TOC may not play mixed CDs. AudioOut errors require
relaunch. Synchronous CAM/HTTP calls can delay input until their timeouts.

See [validation](docs/0.9.1-validation.md), [installation](docs/build-and-install.md)
and [distribution review](docs/dependency-audit.md).

### Completed dependency audit

- Reproduced linked SDK libraries and the Mozilla CA bundle byte-for-byte from
  the supplied sources; added a paired corresponding-source export workflow.
- Completed SDK per-file notices; removed unused C++ runtime archive linkage.
- Included Sri7ach1's GPL-3.0-or-later unjail helper source and separate build.
  Corrected its target selection to require the CD5 path and reject ambiguity.
  This helper change and alternate payload loaders still require PS5 testing.
