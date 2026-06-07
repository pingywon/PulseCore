# 14_Pulse_CoreS3_Demo

## Overview
This folder contains iteration 14 of the PulseCore / Pluto 9000 CoreS3 demo software.

## Main changes
- Locked PPM range to 40–300 PPM.
- Added true pulse-frequency graph behavior.
- Kept open ad-hoc Wi-Fi with no password.
- Improved mobile web layout and added more web options.
- Prevented E-stop from overwriting configured PPM setting.

## Files
- `14_Pulse_CoreS3_Demo.ino` — Arduino IDE sketch for this iteration.

## Notes
- Open the `.ino` file in Arduino IDE from this folder.
- Select the M5Stack CoreS3/CoreS3 SE board profile before uploading.
- Later iterations move closer to live hardware testing; verify wiring and output behavior before connecting valves, solenoids, pumps, or MOSFET boards.
