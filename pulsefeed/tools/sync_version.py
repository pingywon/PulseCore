#!/usr/bin/env python3
"""Stamp VERSION into the firmware's kVersion constant.

kVersion drifted from VERSION for the entire 2.1.0 cycle -- the constant
stayed hardcoded at "2.0.0" while VERSION and CHANGELOG.md moved on, so
the boot screen, the web dashboard and the API all quietly reported the
wrong build. Run before every build so that can't happen again.
"""
import pathlib
import re
import sys

ROOT = pathlib.Path(__file__).resolve().parent.parent
VERSION = (ROOT / "VERSION").read_text().strip()
CORE_H = ROOT / "firmware" / "pulsefeed" / "pf_core.h"

PATTERN = re.compile(
    r'(static const char\* const kVersion\s*=\s*)"[^"]*"(\s*;)'
)


def main():
    src = CORE_H.read_text()
    new, n = PATTERN.subn(rf'\g<1>"{VERSION}"\g<2>', src, count=1)
    if n == 0:
        sys.exit(f"kVersion line not found in {CORE_H}")
    if new != src:
        CORE_H.write_text(new)
        print(f"  kVersion -> {VERSION}")
    else:
        print(f"  kVersion already {VERSION}")


if __name__ == "__main__":
    main()
