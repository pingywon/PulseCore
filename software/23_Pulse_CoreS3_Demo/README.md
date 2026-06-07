# 23_Pulse_CoreS3_Demo

## Overview
This folder contains iteration 23 of the PulseCore / Pluto 9000 CoreS3 demo software.

## Main changes
- Removed PulseCore splash from boot.
- Reworked Milky boot animation to finish in about three seconds.
- Added icon-only theme control.
- Added staged hold behavior: 5-step jumps after 1 second, 10-step jumps after 3 seconds.
- Added pulse waveform behavior to graphs.
- Added pulse demo behavior in Test mode.
- Added broader logging of button presses, state, and system events.

## Files
- `23_Pulse_CoreS3_Demo.ino` — Arduino IDE sketch for this iteration.

## Notes
- Open the `.ino` file in Arduino IDE from this folder.
- Select the M5Stack CoreS3/CoreS3 SE board profile before uploading.
- Later iterations move closer to live hardware testing; verify wiring and output behavior before connecting valves, solenoids, pumps, or MOSFET boards.
