#!/usr/bin/env bash
# =====================================================================
#  PulseFeed build driver
#
#    tools/build.sh test        host unit tests for the portable core
#    tools/build.sh sim         host simulator
#    tools/build.sh web         regenerate the embedded web assets
#    tools/build.sh firmware    compile for M5Stack CoreS3
#    tools/build.sh flash PORT  compile + upload
#    tools/build.sh package     build everything and make the archive
#    tools/build.sh all         test + sim + web + firmware
# =====================================================================
set -euo pipefail

ROOT="$(cd "$(dirname "${BASH_SOURCE[0]}")/.." && pwd)"
cd "$ROOT"

FQBN="esp32:esp32:m5stack_cores3"
CORE="firmware/pulsefeed/pf_core.cpp firmware/pulsefeed/pf_rhythms.cpp"
CXXFLAGS="-std=c++17 -O2 -Wall -Wextra"

export PATH="$PATH:$HOME/.local/bin"
mkdir -p dist

c_grn() { printf '\033[32m%s\033[0m\n' "$*"; }
c_red() { printf '\033[31m%s\033[0m\n' "$*"; }
hdr()   { printf '\n\033[1m== %s ==\033[0m\n' "$*"; }

do_test() {
  hdr "host tests"
  g++ $CXXFLAGS -o dist/pf_test tests/test_core.cpp $CORE
  ./dist/pf_test
}

do_sim() {
  hdr "simulator"
  g++ $CXXFLAGS -pthread -o dist/pulsefeed-sim sim/pulsefeed-sim.cpp $CORE
  c_grn "  dist/pulsefeed-sim"
}

do_web() {
  hdr "web assets"
  python3 tools/build_web.py
}

do_firmware() {
  hdr "firmware"
  command -v arduino-cli >/dev/null || { c_red "arduino-cli not on PATH"; exit 1; }
  arduino-cli compile --fqbn "$FQBN" --output-dir dist/firmware firmware/pulsefeed
  c_grn "  dist/firmware/"
}

do_flash() {
  local port="${1:-/dev/ttyACM0}"
  do_firmware
  hdr "upload -> $port"
  arduino-cli upload -p "$port" --fqbn "$FQBN" firmware/pulsefeed
}

do_package() {
  do_test; do_web; do_sim
  if command -v arduino-cli >/dev/null; then do_firmware; else c_red "  (skipping firmware: no arduino-cli)"; fi
  hdr "archive"
  python3 tools/package.py
}

case "${1:-all}" in
  test)     do_test ;;
  sim)      do_sim ;;
  web)      do_web ;;
  firmware) do_firmware ;;
  flash)    do_flash "${2:-/dev/ttyACM0}" ;;
  package)  do_package ;;
  all)      do_test; do_web; do_sim; do_firmware ;;
  *) sed -n '3,12p' "$0"; exit 1 ;;
esac
