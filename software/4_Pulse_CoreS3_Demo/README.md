# 4_Pulse_CoreS3_Demo

## Overview
This folder contains iteration 4 of the PulseCore / Pluto 9000 CoreS3 demo software.

## Main changes
- Fresh multi-screen layout for the CoreS3 demo.
- Added separate VAC and PPM control screens with graphs and large controls.
- Added full-screen E-stop confirmation behavior.
- Renamed Suck to VAC and allowed 0–100% VAC control.
- Restored darker styling and revised home screen button placement.

## Files
- `4_Pulse_CoreS3_Demo.ino` — Arduino IDE sketch for this iteration.

## Notes
- Open the `.ino` file in Arduino IDE from this folder.
- Select the M5Stack CoreS3/CoreS3 SE board profile before uploading.
- Later iterations move closer to live hardware testing; verify wiring and output behavior before connecting valves, solenoids, pumps, or MOSFET boards.
