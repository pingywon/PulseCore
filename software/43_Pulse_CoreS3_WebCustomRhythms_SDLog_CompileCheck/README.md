# 43 Pulse CoreS3 Web Custom Rhythms SD Log Compile Check

Current firmware package for the PulseCore / Pluto 9000 CoreS3 prototype.

Firmware version string:

```cpp
v43-web-custom-rhythms-sdlog-compilecheck
```

## Files

- `43_Pulse_CoreS3_WebCustomRhythms_SDLog_CompileCheck.ino` — full Arduino sketch source.
- `CHANGES_v43.txt` — version notes.

## Control model

- G5 = CH1 VAC
- G6 = CH2 PPM / rhythm pulse
- G7 = CH3 Motor speed
- G8 = reserved / hidden from normal UI

## Features

- Local-first operation.
- Setup AP + web portal.
- No Bluetooth, BLE, or WiFiProv.
- 20 built-in rhythms.
- 5 custom rhythm slots recorded/configured from the web portal.
- Custom rhythm data is saved durably.
- SD card logging uses `/log/YYYY-MM-DD/YYYY-MM-DD.log`.

## Notes

Use this folder as the latest source of truth unless a newer numbered software folder is added.
