# v43 Pulse CoreS3 Web Custom Rhythms + SD Log Compile Check

Latest firmware package generated in ChatGPT for the PulseCore / Pluto 9000 CoreS3 project.

## Version

`v43-web-custom-rhythms-sdlog-compilecheck`

## Firmware scope

- 3-channel model: `G5 = VAC`, `G6 = PPM/Rhythm`, `G7 = Motor`.
- No Bluetooth / BLE / WiFiProv.
- Local-first boot with setup AP + web portal.
- 20 built-in rhythms plus 5 custom web-recorded rhythm slots.
- Custom rhythms are web-configurable and durably saved.
- SD logging path: `/log/YYYY-MM-DD/YYYY-MM-DD.log`.
- Custom rhythm actions are written to SD logs when SD is present.

## Files

The source package created locally was:

`43_Pulse_CoreS3_WebCustomRhythms_SDLog_CompileCheck.zip`

It contains:

- `43_Pulse_CoreS3_WebCustomRhythms_SDLog_CompileCheck.ino`
- `CHANGES_v43.txt`

## Compile note

A true Arduino/M5Stack compile was not run in the ChatGPT runtime because the M5Stack ESP32 board package/toolchain is not installed there. Static syntax checks with a stubbed Arduino/M5Stack environment passed, and brace/parenthesis/bracket balance was checked.
