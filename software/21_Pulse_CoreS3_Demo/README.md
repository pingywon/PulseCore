# 21_Pulse_CoreS3_Demo

## Overview
This folder contains iteration 21 of the PulseCore / Pluto 9000 CoreS3 demo software.

## Main changes
- Added clear web option to open stats.html from the SD card.
- Added clean fallback page if stats.html is not available.
- Clarified that stats.html is the report while log files are the source data.

## Files
- `21_Pulse_CoreS3_Demo.ino` — Arduino IDE sketch for this iteration.

## Notes
- Open the `.ino` file in Arduino IDE from this folder.
- Select the M5Stack CoreS3/CoreS3 SE board profile before uploading.
- Later iterations move closer to live hardware testing; verify wiring and output behavior before connecting valves, solenoids, pumps, or MOSFET boards.
