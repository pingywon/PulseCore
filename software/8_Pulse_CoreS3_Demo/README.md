# 8_Pulse_CoreS3_Demo

## Overview
This folder contains iteration 8 of the PulseCore / Pluto 9000 CoreS3 demo software.

## Main changes
- Improved hold behavior for VAC and PPM controls.
- Changed backlight to a 10-step scale.
- Reduced refresh flicker by limiting redraw regions.
- Added VAC ramp graph and frequency-style PPM graph.
- Added two live bar graphs for VAC and PPM.

## Files
- `8_Pulse_CoreS3_Demo.ino` — Arduino IDE sketch for this iteration.

## Notes
- Open the `.ino` file in Arduino IDE from this folder.
- Select the M5Stack CoreS3/CoreS3 SE board profile before uploading.
- Later iterations move closer to live hardware testing; verify wiring and output behavior before connecting valves, solenoids, pumps, or MOSFET boards.
