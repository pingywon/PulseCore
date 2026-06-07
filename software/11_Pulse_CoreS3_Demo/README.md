# 11_Pulse_CoreS3_Demo

## Overview
This folder contains iteration 11 of the PulseCore / Pluto 9000 CoreS3 demo software.

## Main changes
- Added ad-hoc Wi-Fi web control concept.
- Added web configuration page for VAC, PPM, theme, backlight, and layout options.
- Added saved settings behavior.
- Added optional SD configuration backup concept.
- Added PPM timing logic based on pulses per minute.

## Files
- `11_Pulse_CoreS3_Demo.ino` — Arduino IDE sketch for this iteration.

## Notes
- Open the `.ino` file in Arduino IDE from this folder.
- Select the M5Stack CoreS3/CoreS3 SE board profile before uploading.
- Later iterations move closer to live hardware testing; verify wiring and output behavior before connecting valves, solenoids, pumps, or MOSFET boards.
