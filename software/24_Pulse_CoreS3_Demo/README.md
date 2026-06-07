# 24_Pulse_CoreS3_Demo

## Overview
This folder contains iteration 24 of the PulseCore / Pluto 9000 CoreS3 demo software.

## Main changes
- Treated on-device and web buttons as live controls.
- Made Pulse test button change color in sync with the actual pulse output state.
- Expanded stats.html into a more useful user report.
- Added running-state logging every 2500 ms.
- Added daily log concept and richer logged values.

## Files
- `24_Pulse_CoreS3_Demo.ino` — Arduino IDE sketch for this iteration.

## Notes
- Open the `.ino` file in Arduino IDE from this folder.
- Select the M5Stack CoreS3/CoreS3 SE board profile before uploading.
- Later iterations move closer to live hardware testing; verify wiring and output behavior before connecting valves, solenoids, pumps, or MOSFET boards.
