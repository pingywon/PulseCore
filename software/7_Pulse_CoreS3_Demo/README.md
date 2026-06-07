# 7_Pulse_CoreS3_Demo

## Overview
This folder contains iteration 7 of the PulseCore / Pluto 9000 CoreS3 demo software.

## Main changes
- Fixed Arduino compile ordering issues for theme/backlight globals.
- Moved app state declarations before functions that reference them.
- Kept hold-to-scroll and settings improvements.

## Files
- `7_Pulse_CoreS3_Demo.ino` — Arduino IDE sketch for this iteration.

## Notes
- Open the `.ino` file in Arduino IDE from this folder.
- Select the M5Stack CoreS3/CoreS3 SE board profile before uploading.
- Later iterations move closer to live hardware testing; verify wiring and output behavior before connecting valves, solenoids, pumps, or MOSFET boards.
