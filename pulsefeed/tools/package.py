#!/usr/bin/env python3
"""Build the distributable archive.

`zip` is not installed on this box, so this uses Python's zipfile. It also
writes a SHA-256 manifest, because a firmware archive people download over
a LAN link should be checkable.
"""
import hashlib
import os
import pathlib
import zipfile

ROOT = pathlib.Path(__file__).resolve().parent.parent
DIST = ROOT / "dist"
NAME = f"pulsefeed-{(ROOT / 'VERSION').read_text().strip()}"

INCLUDE_DIRS = ["firmware", "sim", "tests", "bridge", "site", "web", "tools", "docs"]

# The original v43 sketch lives once, in the repo's archive, rather than being
# duplicated under pulsefeed/. Ship it in the archive anyway -- it is the
# baseline the 2.x reconstruction is meant to be diffed against.
V43_DIR = ROOT.parent / "software" / "43_Pulse_CoreS3_WebCustomRhythms_SDLog_CompileCheck"
INCLUDE_FILES = ["README.md", "LICENSE", "VERSION", "CHANGELOG.md"]

SKIP_SUFFIX = (".o", ".pyc", ".swp")
SKIP_PARTS = {"__pycache__", ".git", "build", "cache"}


def arcrel(p: pathlib.Path) -> pathlib.PurePath:
    """Path as it should appear inside the archive."""
    try:
        return p.relative_to(ROOT)
    except ValueError:
        # Outside pulsefeed/ -- currently only the v43 baseline.
        return pathlib.PurePath("v43_original") / p.relative_to(V43_DIR)


def wanted(p: pathlib.Path) -> bool:
    if p.suffix in SKIP_SUFFIX:
        return False
    return not (SKIP_PARTS & set(p.parts))


def collect():
    out = []
    for d in INCLUDE_DIRS:
        base = ROOT / d
        if not base.is_dir():
            continue
        for p in sorted(base.rglob("*")):
            if p.is_file() and wanted(p):
                out.append(p)
    if V43_DIR.is_dir():
        for p in sorted(V43_DIR.rglob("*")):
            if p.is_file() and wanted(p):
                out.append(p)
    for f in INCLUDE_FILES:
        p = ROOT / f
        if p.is_file():
            out.append(p)
    # Built binaries are handy to ship alongside the source.
    for extra in ("pulsefeed-sim",):
        p = DIST / extra
        if p.is_file():
            out.append(p)
    # Only the artifacts you actually flash. The .elf carries debug symbols
    # and the .map and merged.bin are mostly padding -- together they are
    # ~57 MB and would dominate the download for no benefit. Rebuild locally
    # if you need them for a backtrace.
    fw = DIST / "firmware"
    if fw.is_dir():
        keep = ("pulsefeed.ino.bin", "pulsefeed.ino.bootloader.bin",
                "pulsefeed.ino.partitions.bin")
        for p in sorted(fw.rglob("*")):
            if p.is_file() and p.name in keep:
                out.append(p)
    return out


def main():
    DIST.mkdir(exist_ok=True)
    files = collect()
    zpath = DIST / f"{NAME}.zip"

    lines = []
    with zipfile.ZipFile(zpath, "w", zipfile.ZIP_DEFLATED, compresslevel=9) as z:
        for p in files:
            rel = arcrel(p)
            arc = f"{NAME}/{rel}"
            z.write(p, arc)
            digest = hashlib.sha256(p.read_bytes()).hexdigest()
            lines.append(f"{digest}  {rel}")

    manifest = DIST / "SHA256SUMS.txt"
    manifest.write_text("\n".join(lines) + "\n")

    ztotal = zpath.stat().st_size
    raw = sum(p.stat().st_size for p in files)
    print(f"  {len(files)} files, {raw/1024:.0f} KB raw")
    print(f"  {zpath.relative_to(ROOT)}  {ztotal/1024:.0f} KB")
    print(f"  {manifest.relative_to(ROOT)}")
    print(f"  archive sha256: {hashlib.sha256(zpath.read_bytes()).hexdigest()}")


if __name__ == "__main__":
    main()
