#!/usr/bin/env bash
set -euo pipefail

ROOT="$(cd "$(dirname "${BASH_SOURCE[0]}")/.." && pwd)"
SRC="${1:-src/cd5.c}"

cd "$ROOT"

./scripts/build.sh "$SRC"
./scripts/deploy.sh
