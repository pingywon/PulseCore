# 18_Pulse_CoreS3_Demo

## Overview
This folder contains iteration 18 of the PulseCore / Pluto 9000 CoreS3 demo software.

## Main changes
- Added HOME buttons to VAC and PPM screens.
- Centered E-stop text.
- Cleaned up Milky boot screen to avoid overlapping boot elements.
- Made SD logging setup optional and clearly explained that SD is not required for normal use.
- Adjusted captive portal behavior to reduce forced scroll-to-top behavior.

## Files
- `18_Pulse_CoreS3_Demo.ino` — Arduino IDE sketch for this iteration.

## Notes
- Open the `.ino` file in Arduino IDE from this folder.
- Select the M5Stack CoreS3/CoreS3 SE board profile before uploading.
- Later iterations move closer to live hardware testing; verify wiring and output behavior before connecting valves, solenoids, pumps, or MOSFET boards.
