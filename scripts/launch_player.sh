#!/usr/bin/env bash
set -euo pipefail

PS5_IP="${PS5_IP:?Set PS5_IP explicitly before launch}"
SHELL_PORT="${PS5_SHELL_PORT:-2323}"

APP_ID="PPSA00001"

# Supply a separately built helper that validates the CD5 application path.
# scripts/build_unjail.sh builds the GPL-licensed source supplied here.
UNJAIL_ELF="${CD5_UNJAIL_ELF:?Set CD5_UNJAIL_ELF to the verified autonomous helper}"
if [ ! -f "$UNJAIL_ELF" ]; then
    echo "Missing helper: $UNJAIL_ELF" >&2
    exit 1
fi

PLDMGR_URL="http://${PS5_IP}:8084"

ps5_cmd()
{
    local cmd="$1"

    {
        printf '%s\r\n' "$cmd"
        sleep 1
    } | timeout 4 nc "$PS5_IP" "$SHELL_PORT" 2>/dev/null || true
}

get_app_pid()
{
    ps5_cmd "ps | grep $APP_ID" |
        awk '
            $1 ~ /^[0-9]+$/ &&
            $0 ~ /PPSA00001/ {
                print $1
                exit
            }
        '
}

echo "========================================"
echo " CDDA Player launcher"
echo "========================================"

echo
echo "[1/5] Comprobando instancia anterior..."

OLD_PID="$(get_app_pid || true)"

if [ -n "${OLD_PID:-}" ]; then
    echo "PPSA00001 ya estaba vivo: PID $OLD_PID"
    echo "Cerrándolo..."

    ps5_cmd "kill $OLD_PID" >/dev/null

    for i in $(seq 1 10); do
        sleep 1

        if [ -z "$(get_app_pid || true)" ]; then
            break
        fi
    done
fi

echo
echo "[2/5] Lanzando $APP_ID ..."

ps5_cmd "launch $APP_ID" || true

echo
echo "[3/5] Esperando PID nuevo..."

PID=""

for i in $(seq 1 15); do
    PID="$(get_app_pid || true)"

    if [ -n "$PID" ]; then
        break
    fi

    sleep 1
done

if [ -z "$PID" ]; then
    echo
    echo "ERROR: no apareció el proceso $APP_ID"
    exit 1
fi

echo "PID detectado: $PID"

echo
echo "[4/5] Subiendo helper autónomo ..."


curl -sS \
    -Q "MTRW" \
    "ftp://${PS5_IP}:2121/" \
    >/dev/null

curl -sS \
    --ftp-create-dirs \
    -T "$UNJAIL_ELF" \
    "ftp://${PS5_IP}:2121/data/pldmgr/payloads/unjail_pid/unjail_pid.elf" \
    >/dev/null

echo
echo "[5/5] Inyectando unjail mediante Payload Manager..."

curl -sS \
    -X POST \
    -H 'X-PLDMGR-Request: 1' \
    "${PLDMGR_URL}/loadpayload:unjail_pid%2Funjail_pid.elf"

echo
echo
echo "Esperando aplicación..."

sleep 2

echo
ps5_cmd "ps | grep $APP_ID"

echo
echo "========================================"
echo " CDDA Player lanzado"
echo " PID: $PID"
echo "========================================"
