#!/usr/bin/env python3

from pathlib import Path

ROOT = Path(__file__).resolve().parent.parent
OUT = ROOT / "build" / "ui_assets.c"

ASSETS = [
    ("cd5_icon0_png",
     ROOT / "assets" / "branding" / "icon0.png"),

    ("cd5_font_ubuntu_regular",
     ROOT / "assets" / "fonts" / "Ubuntu-Regular.ttf"),

    ("cd5_font_ubuntu_bold",
     ROOT / "assets" / "fonts" / "Ubuntu-Bold.ttf"),

    ("cd5_font_bebas",
     ROOT / "assets" / "fonts" / "BebasNeue-Regular.ttf"),
]

OUT.parent.mkdir(parents=True, exist_ok=True)

with OUT.open("w") as f:
    f.write("#include <stddef.h>\n\n")

    for name, path in ASSETS:
        data = path.read_bytes()

        f.write(
            f"const unsigned char {name}[] = {{\n"
        )

        for i in range(0, len(data), 16):
            chunk = data[i:i + 16]

            f.write("    ")
            f.write(
                ", ".join(
                    f"0x{b:02x}"
                    for b in chunk
                )
            )
            f.write(",\n")

        f.write("};\n")

        f.write(
            f"const size_t {name}_len = "
            f"{len(data)};\n\n"
        )

        print(
            f"{name}: {len(data)} bytes"
        )
