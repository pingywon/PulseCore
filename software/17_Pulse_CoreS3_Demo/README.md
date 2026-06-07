# 17_Pulse_CoreS3_Demo

## Overview
This folder contains iteration 17 of the PulseCore / Pluto 9000 CoreS3 demo software.

## Main changes
- Fixed compile issue caused by handleSdFormat ordering.
- Added explicit forward declarations for web handlers.
- Kept web dashboard, SD tools, and mobile mode improvements.

## Files
- `17_Pulse_CoreS3_Demo.ino` — Arduino IDE sketch for this iteration.

## Notes
- Open the `.ino` file in Arduino IDE from this folder.
- Select the M5Stack CoreS3/CoreS3 SE board profile before uploading.
- Later iterations move closer to live hardware testing; verify wiring and output behavior before connecting valves, solenoids, pumps, or MOSFET boards.
