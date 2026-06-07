# 26_Pulse_CoreS3_Demo

## Overview
This folder contains iteration 26 of the PulseCore / Pluto 9000 CoreS3 demo software.

## Main changes
- Adjusted VAC screen to focus on current VAC setting and line graph.
- Moved HOME button on VAC screen and made it larger.
- Made Live screen inactive until Start is pressed.
- Updated web Start/Stop into one green/red button.
- Added current IP/share-link logic with port shown only when not 80.
- Added initial WiFi scan and port handling changes.

## Files
- `26_Pulse_CoreS3_Demo.ino` — Arduino IDE sketch for this iteration.

## Notes
- Open the `.ino` file in Arduino IDE from this folder.
- Select the M5Stack CoreS3/CoreS3 SE board profile before uploading.
- Later iterations move closer to live hardware testing; verify wiring and output behavior before connecting valves, solenoids, pumps, or MOSFET boards.
