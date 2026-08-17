#!/usr/bin/env python3
"""Stamp VERSION into the firmware's kVersion constant, and keep the
semver tied to this repo's line number (see ../VERSIONS.md).

kVersion drifted from VERSION for the entire 2.1.0 cycle -- the constant
stayed hardcoded at "2.0.0" while VERSION and CHANGELOG.md moved on, so
the boot screen, the web dashboard and the API all quietly reported the
wrong build. Run before every build so that can't happen again.

The same check now covers the branch: post-43 firmware lives on
testing/v<N> where N = 40 + <semver minor>. Bumping VERSION without
moving to the matching branch (or vice versa) is caught here rather than
discovered after a release is tagged wrong.
"""
import pathlib
import re
import subprocess
import sys

ROOT = pathlib.Path(__file__).resolve().parent.parent
VERSION = (ROOT / "VERSION").read_text().strip()
CORE_H = ROOT / "firmware" / "pulsefeed" / "pf_core.h"

PATTERN = re.compile(
    r'(static const char\* const kVersion\s*=\s*)"[^"]*"(\s*;)'
)

LINE_BASE = 40          # VERSIONS.md: N = LINE_BASE + semver minor


def line_number(semver: str):
    """Repo line number for a semver, or None if it doesn't parse."""
    m = re.match(r"^\d+\.(\d+)\.\d+$", semver)
    return LINE_BASE + int(m.group(1)) if m else None


def current_branch():
    try:
        out = subprocess.run(
            ["git", "rev-parse", "--abbrev-ref", "HEAD"],
            cwd=ROOT, capture_output=True, text=True, timeout=5,
        )
        return out.stdout.strip() if out.returncode == 0 else None
    except (OSError, subprocess.SubprocessError):
        return None        # not a checkout, or no git -- not fatal


def check_branch():
    n = line_number(VERSION)
    if n is None:
        return
    branch = current_branch()
    # Only enforce on testing/v<N> branches. Detached HEAD, main, or an
    # ad-hoc branch shouldn't block a build.
    if not branch:
        return
    m = re.match(r"^testing/v(\d+)$", branch)
    if not m:
        print(f"  line v{n} (branch '{branch}' is not testing/v{n} -- not enforced)")
        return
    if int(m.group(1)) != n:
        sys.exit(
            f"version/branch mismatch: VERSION {VERSION} implies line v{n} "
            f"(40 + minor), but this is branch '{branch}'.\n"
            f"Either bump VERSION to 2.{int(m.group(1)) - LINE_BASE}.x or "
            f"work on testing/v{n}. See VERSIONS.md."
        )
    print(f"  line v{n} matches branch {branch}")


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
    check_branch()


if __name__ == "__main__":
    main()
