# PulseCore / Pluto 9000

Software archive for the PulseCore / Pluto 9000 CoreS3 project.

See [VERSIONS.md](VERSIONS.md) for the full version map and the branch rule.

## Current firmware — 43

`software/43_Pulse_CoreS3_WebCustomRhythms_SDLog_CompileCheck/`
(`v43-web-custom-rhythms-sdlog-compilecheck`)

The last of the original numbered line, and what this branch treats as
current. Versions 4–27 are archived alongside it; 28–42 are not archived
anywhere known.

Known issue, carried as-is: v43 **does not link** as shipped —
`changeRhythmSpeed(int)` is declared and called but never defined.

## Work after 43 lives on branches

Anything past 43 is testing/diagnostic firmware and does not land on `main`.
Each version gets its own branch:

| Branch | Version | What |
|---|---|---|
| [`testing/v44`](../../tree/testing/v44) | PulseFeed 2.4.0 | Full audit + reconstruction of v43. Arduino-free engine core shared by 89 host tests and a host simulator; web dashboard as the primary control surface; service/test modes incl. a PPM pin test. |

Build from a testing branch, not here:

```bash
git checkout testing/v44
cd pulsefeed && tools/build.sh package
```

## Control model

- G5 = CH1 VAC
- G6 = CH2 PPM / rhythm pulse
- G7 = CH3 Motor speed
- G8 = reserved / hidden from normal UI

## Direction

- 3 visible channels only.
- No Bluetooth, BLE, or WiFiProv.
- Local-first boot: touchscreen control works without waiting for WiFi.
- Setup AP + web portal for WiFi and control.
- Custom rhythms are saved durably and can be backed up to SD.
- SD logs use `/log/YYYY-MM-DD/YYYY-MM-DD.log`.

43 ships 20 built-in rhythms and 5 web-recorded custom slots. The count is
higher on the testing branches — check the branch, not this file, for what a
given build actually has.

## Mascot and diagram source art

`docs/assets/` is the single home for the source renders. The testing branches
read from here rather than carrying their own copy of the art.

## Hardware note

Output polarity has **not** been verified on real hardware. Confirm on a meter
with loads disconnected before enabling PWM on a channel — guessing wrong
energises channels. Older hardware notes may mention other pin mappings and
should not override the firmware you are actually flashing without deliberate
review.
