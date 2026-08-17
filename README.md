# PulseCore / Pluto 9000 — `testing/v44`

> **Testing / diagnostic branch.** This is line **v44** (PulseFeed **2.4.0**).
> `main` carries 43, the current/stable firmware. See
> [VERSIONS.md](VERSIONS.md) for the version map and the branch rule
> (`N = 40 + semver minor`, enforced at build time).
>
> Not verified on real hardware beyond a first crash-fix pass — treat every
> build from this branch as diagnostic until it has been flashed and driven.

## Firmware on this branch — PulseFeed 2.4.0

`pulsefeed/` — a full audit and reconstruction of the v43 sketch. The engine
core is Arduino-free C++ so `pulsefeed/tests/` and `pulsefeed/sim/` link the
exact same `pf_core.cpp` the firmware does (89 host tests). Adds a web
dashboard as the primary control surface, service/test modes including a PPM
pin test, motor soft start, a deadman timer and a run limit.

Build with `cd pulsefeed && tools/build.sh package`.

## Where 43 lives

`software/43_Pulse_CoreS3_WebCustomRhythms_SDLog_CompileCheck/`
(`v43-web-custom-rhythms-sdlog-compilecheck`) — carried on this branch too, for
diffing against the reconstruction. It is the current firmware on `main`.

Note that v43 does not link as shipped (`changeRhythmSpeed(int)` is declared
and called but never defined). Versions 28–42 are not archived here.

## Mascot and diagram source art

`docs/assets/` is the single home for the source renders. `pulsefeed/` does not
carry its own copy; `pulsefeed/tools/build_assets.py` reads from here by default
and emits the web-sized WebP set plus the device sprite header.

## Current control model

- G5 = CH1 VAC
- G6 = CH2 PPM / rhythm pulse
- G7 = CH3 Motor speed
- G8 = reserved / hidden from normal UI

## Current direction

- 3 visible channels only.
- No Bluetooth, BLE, or WiFiProv.
- Local-first boot: touchscreen control works without waiting for WiFi.
- Setup AP + web portal for WiFi and control.
- 30 built-in rhythms plus 10 custom slots. A slot can start from a tap
  recording or a copy of any built-in, then be renamed and re-tuned — no
  rhythm is fixed forever.
- Custom rhythms are saved durably and can be backed up to SD.
- SD logs use `/log/YYYY-MM-DD/YYYY-MM-DD.log`.

## Hardware note

The pin truth above is what `pulsefeed/firmware/pulsefeed/pf_config.h` actually
compiles. Older hardware notes may mention other pin mappings and should not
override the active firmware without deliberate review.

Output polarity has **not** been verified on real hardware. `PF_OFF_MODE`
deliberately reproduces v43's behaviour because guessing wrong energises
channels. Confirm on a meter with loads disconnected before enabling PWM on a
channel.
