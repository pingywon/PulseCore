# 20_Pulse_CoreS3_Demo

## Overview
This folder contains iteration 20 of the PulseCore / Pluto 9000 CoreS3 demo software.

## Main changes
- Removed SD formatter input field to stop mobile keyboards from appearing.
- Moved SD setup under More > SD Card Logging.
- Changed SD setup to button-based confirmation.
- Updated main home layout with VAC, PPM, Start/Stop, and More.

## Files
- `20_Pulse_CoreS3_Demo.ino` — Arduino IDE sketch for this iteration.

## Notes
- Open the `.ino` file in Arduino IDE from this folder.
- Select the M5Stack CoreS3/CoreS3 SE board profile before uploading.
- Later iterations move closer to live hardware testing; verify wiring and output behavior before connecting valves, solenoids, pumps, or MOSFET boards.
