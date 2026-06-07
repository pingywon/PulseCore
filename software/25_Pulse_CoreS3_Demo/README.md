# 25_Pulse_CoreS3_Demo

## Overview
This folder contains iteration 25 of the PulseCore / Pluto 9000 CoreS3 demo software.

## Main changes
- Removed master log concept.
- Moved daily logs into /logs.
- Changed daily log format to /logs/MM-DD-YYYY.log.
- Kept HH-MM-SS timestamps inside log entries.
- Updated stats.html to reference daily logs as the source of truth.

## Files
- `25_Pulse_CoreS3_Demo.ino` — Arduino IDE sketch for this iteration.

## Notes
- Open the `.ino` file in Arduino IDE from this folder.
- Select the M5Stack CoreS3/CoreS3 SE board profile before uploading.
- Later iterations move closer to live hardware testing; verify wiring and output behavior before connecting valves, solenoids, pumps, or MOSFET boards.
