#!/usr/bin/env bash
set -euo pipefail

ROOT="$(cd "$(dirname "${BASH_SOURCE[0]}")/.." && pwd)"
NATIVE="${CD5_NATIVE_SDK:-/root/ps5dev/PS5SX2/ps5/proto/native}"
SDK_IMAGE="${CD5_SDK_IMAGE:-ps5-payload-sdk}"

if [ ! -f "$NATIVE/tooling/prospero-clang18" ]; then
    echo "ERROR: herramientas nativas PS5 no encontradas en: $NATIVE"
    echo "Configura CD5_NATIVE_SDK con la ruta correcta."
    exit 1
fi

if [ ! -f "$ROOT/runtime/app_crt.o" ]; then
    echo "ERROR: falta runtime/app_crt.o"
    echo "Se requiere el runtime nativo compatible para compilar CD5."
    exit 1
fi

SRC="${1:-src/cd5.c}"

BUILD_DIR="$ROOT/build"
PACKAGE_DIR="$ROOT/package/PPSA00001"

cd "$ROOT"

if [ ! -f "$SRC" ]; then
    echo "ERROR: no existe $ROOT/$SRC"
    exit 1
fi

mkdir -p "$BUILD_DIR"
mkdir -p "$PACKAGE_DIR"

echo "[1/4] Compilando $SRC ..."

python3 scripts/embed_ui_assets.py

docker run --rm \
  -v "$ROOT":/work \
  -v "$NATIVE":/native \
  -w /work \
  -e PS5_PAYLOAD_SDK=/opt/ps5-payload-sdk \
  -e PS5_CLANG=/usr/bin/clang-18 \
  "$SDK_IMAGE" \
  sh -c "
    /native/tooling/prospero-clang18 \
      -O2 \
      -Ithird_party/stb \
      -Ithird_party/jsmn \
      -ffunction-sections \
      -fdata-sections \
      -c '$SRC' \
      -o build/cd5.o

    /native/tooling/prospero-clang18 \
      -O2 \
      -fvisibility=hidden \
      -ffunction-sections \
      -fdata-sections \
      -c src/net.c \
      -o build/net.o

    /native/tooling/prospero-clang18 \
      -O2 \
      -fvisibility=hidden \
      -ffunction-sections \
      -fdata-sections \
      -c src/discid.c \
      -o build/discid.o


    /native/tooling/prospero-clang18 \
      -O2 \
      -fvisibility=hidden \
      -Ithird_party/jsmn \
      -ffunction-sections \
      -fdata-sections \
      -c src/metadata.c \
      -o build/metadata.o


    /native/tooling/prospero-clang18 \
      -O2 \
      -fvisibility=hidden \
      -Ithird_party/jsmn \
      -ffunction-sections \
      -fdata-sections \
      -c src/lyrics.c \
      -o build/lyrics.o


    /native/tooling/prospero-clang18 \
      -O2 \
      -fvisibility=hidden \
      -Ithird_party/stb \
      -ffunction-sections \
      -fdata-sections \
      -c src/cover.c \
      -o build/cover.o

    /native/tooling/prospero-clang18 \
      -O2 \
      -fvisibility=hidden \
      -ffunction-sections \
      -fdata-sections \
      -c build/ui_assets.c \
      -o build/ui_assets.o

  "

echo "[CA] Embebiendo bundle de certificados ..."

docker run --rm \
  -v "$ROOT":/work \
  -w /work \
  "$SDK_IMAGE" \
  python3 -c '
from pathlib import Path

src = Path("/opt/ps5-payload-sdk/target/etc/ca-bundle.crt")
dst = Path("/work/build/ca_bundle.c")

data = src.read_bytes()

with dst.open("w") as f:
    f.write("#include <stddef.h>\n\n")
    f.write("const unsigned char cd5_ca_bundle[] = {\n")

    for i in range(0, len(data), 16):
        chunk = data[i:i + 16]
        f.write("    ")
        f.write(", ".join("0x%02x" % b for b in chunk))
        f.write(",\n")

    f.write("};\n\n")
    f.write(
        "const size_t cd5_ca_bundle_len = "
        + str(len(data))
        + ";\n"
    )

print("CA bundle embebido:", len(data), "bytes")
'

docker run --rm \
  -v "$ROOT":/work \
  -v "$NATIVE":/native \
  -w /work \
  "$SDK_IMAGE" \
  sh -c '
    /native/tooling/prospero-clang18 \
      -O2 \
      -fvisibility=hidden \
      -ffunction-sections \
      -fdata-sections \
      -c build/ca_bundle.c \
      -o build/ca_bundle.o
  '

echo "[2/4] Enlazando ..."

docker run --rm \
  -v "$ROOT":/work \
  -v "$NATIVE":/native \
  -w /work \
  "$SDK_IMAGE" \
  sh -c '
    STUBS=$(find /opt/ps5-payload-sdk/target/lib -maxdepth 1 -name "*.so" \
      ! -name "libScePosixForWebKit.so" \
      ! -name "libSceGLSlimVSH.so" | sort)

    ld.lld-18 \
      -m elf_x86_64 \
      -pie \
      -z max-page-size=0x4000 \
      -mllvm -emulated-tls \
      --hash-style=gnu \
      -T /native/tooling/native/ps5-pie.ld \
      --eh-frame-hdr \
      --version-script /native/tooling/native/app-symbols.map \
      -e _start \
      --no-dynamic-linker \
      -o build/llvm-pie.elf \
      runtime/app_crt.o \
      build/cd5.o \
      build/net.o \
      build/discid.o \
      build/metadata.o \
      build/lyrics.o \
      build/cover.o \
      build/ui_assets.o \
      build/ca_bundle.o \
      --as-needed $STUBS \
      --exclude-libs=ALL \
      -L /opt/ps5-payload-sdk/target/lib \
      /opt/ps5-payload-sdk/target/lib/libc++.a \
      /opt/ps5-payload-sdk/target/lib/libc++abi.a \
      /opt/ps5-payload-sdk/target/lib/libunwind.a \
      --start-group \
      /opt/ps5-payload-sdk/target/lib/libcurl.a \
      /opt/ps5-payload-sdk/target/lib/libmbedtls.a \
      /opt/ps5-payload-sdk/target/lib/libmbedx509.a \
      /opt/ps5-payload-sdk/target/lib/libmbedcrypto.a \
      /opt/ps5-payload-sdk/target/lib/libc.a \
      --end-group \
      -lpthread
  '

echo "[3/4] Convirtiendo y firmando ..."

docker run --rm \
  -v "$ROOT":/work \
  -v "$NATIVE":/native \
  -w /work \
  "$SDK_IMAGE" \
  sh -c '
    /native/tooling/native/ps5-native-tool link \
      --in build/llvm-pie.elf \
      --out build/eboot-native.elf \
      --stub-dir /opt/ps5-payload-sdk/target/lib \
      --module-sdk 0x02000009 \
      --companion-sdk 0x08050001 \
      --file-name eboot.elf

    /native/tooling/native/ps5-native-tool self \
      --sign \
      --in build/eboot-native.elf \
      --out build/eboot.bin \
      --magic 0x1D3D154F
  '

echo "[4/4] Preparando paquete ..."

mkdir -p package/PPSA00001/sce_sys

cp -f build/eboot.bin \
  package/PPSA00001/eboot.bin

cp -f assets/branding/icon0.png \
  package/PPSA00001/sce_sys/icon0.png

cp -f assets/package/param.json \
  package/PPSA00001/sce_sys/param.json

echo
echo "BUILD OK"
echo "Fuente: $SRC"
echo "Salida:"
sha256sum package/PPSA00001/eboot.bin
