#!/usr/bin/env bash
set -euo pipefail

ROOT="$(cd "$(dirname "${BASH_SOURCE[0]}")/.." && pwd)"
PS5_IP="${PS5_IP:?Set PS5_IP explicitly before deployment}"

LOCAL_EBOOT="$ROOT/package/PPSA00001/eboot.bin"
REMOTE_EBOOT="/system_ex/app/PPSA00001/eboot.bin"

if [ ! -f "$LOCAL_EBOOT" ]; then
    echo "ERROR: no existe $LOCAL_EBOOT"
    echo "Ejecuta primero: ./scripts/build.sh"
    exit 1
fi

echo "[1/2] Habilitando escritura FTP ..."

curl -Q "MTRW" \
  "ftp://$PS5_IP:2121/" \
  > /dev/null

echo "[2/2] Subiendo eboot.bin ..."

curl -T "$LOCAL_EBOOT" \
  "ftp://$PS5_IP:2121$REMOTE_EBOOT"

echo
echo "DEPLOY OK"
echo "Destino: $REMOTE_EBOOT"
echo "SHA256:"
sha256sum "$LOCAL_EBOOT"
