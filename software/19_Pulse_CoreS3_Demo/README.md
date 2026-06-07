# 19_Pulse_CoreS3_Demo

## Overview
This folder contains iteration 19 of the PulseCore / Pluto 9000 CoreS3 demo software.

## Main changes
- Fixed Live screen Back and Start/Stop behavior.
- Changed Live screen to line graphs for VAC and PPM.
- Added SD samples every 2500 ms.
- Changed stats.html into a viewer/report based on log data.
- Added More > WiFi control for webserver on/off.

## Files
- `19_Pulse_CoreS3_Demo.ino` — Arduino IDE sketch for this iteration.

## Notes
- Open the `.ino` file in Arduino IDE from this folder.
- Select the M5Stack CoreS3/CoreS3 SE board profile before uploading.
- Later iterations move closer to live hardware testing; verify wiring and output behavior before connecting valves, solenoids, pumps, or MOSFET boards.
