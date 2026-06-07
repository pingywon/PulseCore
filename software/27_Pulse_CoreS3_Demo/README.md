# 27_Pulse_CoreS3_Demo

## Overview
This folder contains iteration 27 of the PulseCore / Pluto 9000 CoreS3 demo software.

## Main changes
- Reworked WiFi architecture around fallback ad-hoc mode and router connection mode.
- Kept Pluto9000 ad-hoc fallback active during router setup.
- Improved WiFi scan/connect workflow.
- Saved router credentials only after successful connection.
- Added clearer WiFi status reporting for SSID, IP, port, and share link.
- Improved website WiFi section structure and messaging.

## Files
- `27_Pulse_CoreS3_Demo.ino` — Arduino IDE sketch for this iteration.

## Notes
- Open the `.ino` file in Arduino IDE from this folder.
- Select the M5Stack CoreS3/CoreS3 SE board profile before uploading.
- Later iterations move closer to live hardware testing; verify wiring and output behavior before connecting valves, solenoids, pumps, or MOSFET boards.
