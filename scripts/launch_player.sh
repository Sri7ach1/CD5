#!/usr/bin/env bash
set -euo pipefail

PS5_IP="${PS5_IP:-192.168.0.2}"
SHELL_PORT="2323"

APP_ID="PPSA00001"

UNJAIL_DIR="${CD5_UNJAIL_DIR:-/root/ps5dev/build/unjail_pid}"
UNJAIL_SRC="$UNJAIL_DIR/unjail_pid.c"
UNJAIL_ELF="$UNJAIL_DIR/unjail_pid.elf"

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
echo "[1/7] Comprobando instancia anterior..."

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
echo "[2/7] Lanzando $APP_ID ..."

ps5_cmd "launch $APP_ID" || true

echo
echo "[3/7] Esperando PID nuevo..."

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
echo "[4/7] Preparando unjail para PID $PID ..."

cd "$UNJAIL_DIR"

sed -i \
    "s/#define TARGET_PID .*/#define TARGET_PID $PID/" \
    "$UNJAIL_SRC"

grep TARGET_PID "$UNJAIL_SRC"

echo
echo "[5/7] Compilando helper..."

docker run --rm \
    -v "$UNJAIL_DIR:/src" \
    -w /src \
    ps5-payload-sdk \
    make clean all

if [ ! -f "$UNJAIL_ELF" ]; then
    echo "ERROR: no se generó $UNJAIL_ELF"
    exit 1
fi

echo
echo "[6/7] Subiendo helper..."

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
echo "[7/7] Inyectando unjail mediante Payload Manager..."

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
