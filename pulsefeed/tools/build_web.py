#!/usr/bin/env python3
"""
Embed the web UI into the firmware as gzipped PROGMEM byte arrays.

v43 built its ~22 KB dashboard by concatenating an Arduino String on
every single request, then handed that String to server.send(), which
copied it again. Two heap allocations of tens of kilobytes per page
load, on a device whose control loop shares that heap.

Serving a pre-gzipped constant out of flash costs zero heap, and the
payload over the wire drops by roughly 75%.
"""
import gzip
import pathlib
import re
import sys

ROOT = pathlib.Path(__file__).resolve().parent.parent
SRC = ROOT / "web" / "src"
OUT = ROOT / "firmware" / "pulsefeed" / "pf_web.h"

ASSETS = [
    ("index.html", "PF_INDEX_GZ"),
    ("stats.html", "PF_STATS_GZ"),
]


def minify(text: str) -> str:
    """Conservative whitespace squeeze.

    Deliberately NOT a real minifier: it only touches leading indentation
    and blank lines, so it cannot break a template literal or a regex.
    gzip does the actual heavy lifting.
    """
    out = []
    for line in text.split("\n"):
        stripped = line.strip()
        if not stripped:
            continue
        out.append(stripped)
    return "\n".join(out)


def emit(name: str, symbol: str, data: bytes, fh) -> int:
    packed = gzip.compress(data, 9)
    fh.write(f"\n// {name}: {len(data)} bytes raw -> {len(packed)} bytes gzipped\n")
    fh.write(f"static const size_t {symbol}_LEN = {len(packed)};\n")
    fh.write(f"static const uint8_t {symbol}[] PROGMEM = {{\n")
    for i in range(0, len(packed), 16):
        chunk = packed[i:i + 16]
        fh.write("  " + ",".join(f"0x{b:02x}" for b in chunk) + ",\n")
    fh.write("};\n")
    return len(packed)


def main() -> int:
    if not SRC.is_dir():
        print(f"error: {SRC} not found", file=sys.stderr)
        return 1

    total = 0
    with OUT.open("w") as fh:
        fh.write("// ===================================================================\n")
        fh.write("//  pf_web.h -- GENERATED FILE, DO NOT EDIT\n")
        fh.write("//  Source: web/src/*.html    Regenerate: tools/build_web.py\n")
        fh.write("// ===================================================================\n")
        fh.write("#pragma once\n#include <Arduino.h>\n#include <stddef.h>\n")

        for filename, symbol in ASSETS:
            path = SRC / filename
            if not path.is_file():
                print(f"error: missing {path}", file=sys.stderr)
                return 1
            raw = minify(path.read_text(encoding="utf-8")).encode("utf-8")
            packed_len = emit(filename, symbol, raw, fh)
            total += packed_len
            pct = 100 * packed_len / len(raw)
            print(f"  {filename:14} {len(raw):6,} B -> {packed_len:6,} B gz  ({pct:.0f}%)")

    print(f"  {'TOTAL':14} {'':6}    {total:6,} B in flash")
    print(f"  wrote {OUT.relative_to(ROOT)}")
    return 0


if __name__ == "__main__":
    raise SystemExit(main())
