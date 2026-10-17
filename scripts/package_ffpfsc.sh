#!/usr/bin/env bash
set -euo pipefail

ROOT="$(cd "$(dirname "${BASH_SOURCE[0]}")/.." && pwd)"
APP_DIR="$ROOT/package/PPSA00001"
DIST_DIR="$ROOT/dist"
OUTPUT="$DIST_DIR/CD5.ffpfsc"

cd "$ROOT"

if [ ! -d "$APP_DIR" ]; then
    echo "ERROR: no existe $APP_DIR"
    exit 1
fi

if [ ! -f "$APP_DIR/eboot.bin" ]; then
    echo "ERROR: falta $APP_DIR/eboot.bin"
    echo "Ejecuta primero: ./scripts/build.sh"
    exit 1
fi

if [ ! -f "$APP_DIR/sce_sys/param.json" ]; then
    echo "ERROR: falta $APP_DIR/sce_sys/param.json"
    exit 1
fi

MKPFS="$("$ROOT/tools/setup-packaging-dependencies.sh" ffpfsc)"

mkdir -p "$DIST_DIR"
rm -f "$OUTPUT"

echo "[1/2] Generando CD5.ffpfsc ..."

"$MKPFS" pack folder \
    --no-adjust-output-file-extension \
    --version PS5 \
    --verify \
    "$APP_DIR" \
    "$OUTPUT"

echo "[2/2] Verificando salida ..."

test -s "$OUTPUT"

echo
echo "PACKAGE OK"
ls -lh "$OUTPUT"
echo "SHA256:"
sha256sum "$OUTPUT"
