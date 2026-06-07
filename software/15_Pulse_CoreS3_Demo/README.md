# 15_Pulse_CoreS3_Demo

## Overview
This folder contains iteration 15 of the PulseCore / Pluto 9000 CoreS3 demo software.

## Main changes
- Added SD log file creation.
- Added stats.html report creation on the SD card.
- Added web routes for log/status/report access.
- Logged boot, settings save, run start/stop, emergency release, and snapshots.

## Files
- `15_Pulse_CoreS3_Demo.ino` — Arduino IDE sketch for this iteration.

## Notes
- Open the `.ino` file in Arduino IDE from this folder.
- Select the M5Stack CoreS3/CoreS3 SE board profile before uploading.
- Later iterations move closer to live hardware testing; verify wiring and output behavior before connecting valves, solenoids, pumps, or MOSFET boards.
