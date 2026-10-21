#!/usr/bin/env bash
set -euo pipefail
ROOT="$(cd "$(dirname "${BASH_SOURCE[0]}")/.." && pwd)"
mkdir -p "$ROOT/build"
docker run --rm -v "$ROOT":/work -w /work "${CD5_SDK_IMAGE:-ps5-payload-sdk}"   /opt/ps5-payload-sdk/bin/prospero-clang -O2 -Wall -Wextra   helpers/unjail_pid/unjail_pid.c -o build/unjail_pid.elf
sha256sum "$ROOT/build/unjail_pid.elf"
