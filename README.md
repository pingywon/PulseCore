# PulseCore / Pluto 9000

Software archive for the PulseCore / Pluto 9000 CoreS3 project.

## Latest firmware

Latest source package: `software/43_Pulse_CoreS3_WebCustomRhythms_SDLog_CompileCheck/`

Current firmware version: `v43-web-custom-rhythms-sdlog-compilecheck`

## Current control model

- G5 = CH1 VAC
- G6 = CH2 PPM / rhythm pulse
- G7 = CH3 Motor speed
- G8 = reserved / hidden from normal UI

## Current direction

- 3 visible channels only.
- No Bluetooth, BLE, or WiFiProv.
- Local-first boot: touchscreen control works without waiting for WiFi.
- Setup AP + web portal for WiFi and control.
- 20 built-in rhythms plus 5 web-recorded custom rhythm slots.
- Custom rhythms are saved durably and can be backed up to SD.
- SD logs use `/log/YYYY-MM-DD/YYYY-MM-DD.log`.

## Hardware note

The current firmware follows the current sketch pin truth. Older hardware notes may mention other pin mappings and should not override the latest firmware folder without deliberate review.
