#!/usr/bin/env python3
"""Turn the source mascot renders into shippable assets.

The originals from the plusecore repo are ~1 MB PNGs at 1024x1536 -- 40 MB
for the set. That is fine as source art and useless as a deliverable: it
would dominate a page load and cannot go near a 320x240 device at all.

Two outputs:
  site/assets/mascot/*.webp   web-sized, transparent, ~40-70 KB each
  firmware/pulsefeed/pf_milky.h   a handful of moods as RGB565 sprites

Run: python3 tools/build_assets.py [--src DIR]
"""
import argparse
import pathlib
import sys

try:
    from PIL import Image
except ImportError:
    sys.exit("Pillow is required: python3 -c 'import PIL' must work")

ROOT = pathlib.Path(__file__).resolve().parent.parent
WEB_OUT = ROOT / "site" / "assets" / "mascot"
FW_OUT = ROOT / "firmware" / "pulsefeed" / "pf_milky.h"

WEB_HEIGHT = 620          # tall enough for a hero, small enough to ship
WEB_QUALITY = 82

# The device carries only the moods the UI actually switches between.
# Each costs height*width*2 bytes of flash, so this list stays short.
DEVICE_MOODS = [
    ("idle",      "milky-standing-happy.png"),
    ("running",   "milky-thumbs-up.png"),
    ("working",   "milky-hands-on-hips-01.png"),
    ("tired",     "milky-tired-standing.png"),
    ("spent",     "milky-exhausted-seated.png"),
    ("estop",     "milky-shrug-01.png"),
]
DEVICE_H = 96             # px; width follows the source aspect ratio


def trim_alpha(im: Image.Image) -> Image.Image:
    """Crop to the visible subject.

    The renders sit on a lot of empty transparent canvas. Cropping first
    means the height budget is spent on the character rather than on air.
    """
    if im.mode != "RGBA":
        im = im.convert("RGBA")
    bbox = im.split()[-1].getbbox()
    return im.crop(bbox) if bbox else im


def build_web(src: pathlib.Path) -> int:
    WEB_OUT.mkdir(parents=True, exist_ok=True)
    total_in = total_out = 0
    n = 0
    for p in sorted(src.glob("*.png")):
        im = trim_alpha(Image.open(p))
        w, h = im.size
        nw = max(1, round(w * WEB_HEIGHT / h))
        im = im.resize((nw, WEB_HEIGHT), Image.LANCZOS)
        out = WEB_OUT / (p.stem + ".webp")
        im.save(out, "WEBP", quality=WEB_QUALITY, method=6)
        total_in += p.stat().st_size
        total_out += out.stat().st_size
        n += 1
    print(f"  web:    {n} images  {total_in/1024/1024:.1f} MB -> "
          f"{total_out/1024/1024:.2f} MB  ({100*total_out/total_in:.0f}%)")
    return n


def rgb565(r, g, b):
    return ((r & 0xF8) << 8) | ((g & 0xFC) << 3) | (b >> 3)


def build_device(src: pathlib.Path) -> int:
    """Emit RGB565 sprites plus a 1-bit alpha mask.

    M5GFX can push a sprite with a transparent colour, but a mascot with
    white in it cannot spare a colour key -- so the mask is separate and
    explicit rather than stealing a pixel value.
    """
    lines = [
        "// ===================================================================",
        "//  pf_milky.h -- GENERATED FILE, DO NOT EDIT",
        "//  Milky mood sprites, RGB565 + 1bpp alpha mask.",
        "//  Source art: plusecore/docs/assets   Regenerate: tools/build_assets.py",
        "// ===================================================================",
        "#pragma once",
        "#include <Arduino.h>",
        "#include <stdint.h>",
        "",
        "struct MilkySprite {",
        "  const uint16_t* pixels;",
        "  const uint8_t*  mask;   // 1 bit per pixel, row-padded to bytes",
        "  int16_t w, h;",
        "};",
        "",
    ]
    made = 0
    total = 0
    for name, filename in DEVICE_MOODS:
        p = src / filename
        if not p.is_file():
            print(f"    ! missing {filename}, skipping mood '{name}'")
            continue
        im = trim_alpha(Image.open(p))
        w, h = im.size
        nw = max(1, round(w * DEVICE_H / h))
        im = im.resize((nw, DEVICE_H), Image.LANCZOS)
        px = im.load()

        pixels, mask = [], []
        stride = (nw + 7) // 8
        for y in range(DEVICE_H):
            row = bytearray(stride)
            for x in range(nw):
                r, g, b, a = px[x, y]
                pixels.append(rgb565(r, g, b))
                if a >= 128:
                    row[x >> 3] |= 0x80 >> (x & 7)
            mask.extend(row)

        total += len(pixels) * 2 + len(mask)
        lines.append(f"// {name}: {filename}  {nw}x{DEVICE_H}")
        lines.append(f"static const uint16_t MILKY_{name.upper()}_PX[] PROGMEM = {{")
        for i in range(0, len(pixels), 12):
            lines.append("  " + ",".join(f"0x{v:04x}" for v in pixels[i:i + 12]) + ",")
        lines.append("};")
        lines.append(f"static const uint8_t MILKY_{name.upper()}_MASK[] PROGMEM = {{")
        for i in range(0, len(mask), 16):
            lines.append("  " + ",".join(f"0x{v:02x}" for v in mask[i:i + 16]) + ",")
        lines.append("};")
        lines.append(f"static const MilkySprite MILKY_{name.upper()} = {{"
                     f" MILKY_{name.upper()}_PX, MILKY_{name.upper()}_MASK, {nw}, {DEVICE_H} }};")
        lines.append("")
        made += 1

    lines.append("enum MilkyMood { MILKY_MOOD_IDLE = 0, MILKY_MOOD_RUNNING, MILKY_MOOD_WORKING,")
    lines.append("                 MILKY_MOOD_TIRED, MILKY_MOOD_SPENT, MILKY_MOOD_ESTOP,")
    lines.append("                 MILKY_MOOD_COUNT };")
    lines.append("static const MilkySprite* const MILKY_MOODS[MILKY_MOOD_COUNT] = {")
    for name, _ in DEVICE_MOODS:
        lines.append(f"  &MILKY_{name.upper()},")
    lines.append("};")
    lines.append("")

    FW_OUT.write_text("\n".join(lines))
    print(f"  device: {made} moods  {total/1024:.0f} KB of flash  -> "
          f"{FW_OUT.relative_to(ROOT)}")
    return made


def main():
    ap = argparse.ArgumentParser()
    # The source renders live once in this repo, at docs/assets (they came from
    # the old plusecore archive). Do not re-copy 41 MB of PNGs under pulsefeed/.
    ap.add_argument("--src", default=str(ROOT.parent / "docs" / "assets"),
                    help="directory of source PNG renders")
    args = ap.parse_args()
    src = pathlib.Path(args.src)
    if not src.is_dir():
        sys.exit(f"source art not found: {src}")
    print(f"  source: {src}")
    build_web(src)
    build_device(src)


if __name__ == "__main__":
    main()
