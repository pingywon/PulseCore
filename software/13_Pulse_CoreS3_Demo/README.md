# 13_Pulse_CoreS3_Demo

## Overview
This folder contains iteration 13 of the PulseCore / Pluto 9000 CoreS3 demo software.

## Main changes
- Fixed loop delay/webserver responsiveness issue.
- Debounced settings persistence to reduce flash/SD write wear.
- Started Wi-Fi after boot animation to avoid early connection timeouts.
- Removed screen redraw calls from HTTP handlers.
- Improved button text centering using display text width.
- Made screen visibility settings functional.
- Expanded the web dashboard and status reporting.

## Files
- `13_Pulse_CoreS3_Demo.ino` — Arduino IDE sketch for this iteration.

## Notes
- Open the `.ino` file in Arduino IDE from this folder.
- Select the M5Stack CoreS3/CoreS3 SE board profile before uploading.
- Later iterations move closer to live hardware testing; verify wiring and output behavior before connecting valves, solenoids, pumps, or MOSFET boards.
