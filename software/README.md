# PulseCore / Pluto 9000 CoreS3 Software Archive

This archive contains the numbered CoreS3 software iterations, each in its own folder.

## Included versions
- `4_Pulse_CoreS3_Demo/`
- `5_Pulse_CoreS3_Demo/`
- `6_Pulse_CoreS3_Demo/`
- `7_Pulse_CoreS3_Demo/`
- `8_Pulse_CoreS3_Demo/`
- `9_Pulse_CoreS3_Demo/`
- `10_Pulse_CoreS3_Demo/`
- `11_Pulse_CoreS3_Demo/`
- `12_Pulse_CoreS3_Demo/`
- `13_Pulse_CoreS3_Demo/`
- `14_Pulse_CoreS3_Demo/`
- `15_Pulse_CoreS3_Demo/`
- `16_Pulse_CoreS3_Demo/`
- `17_Pulse_CoreS3_Demo/`
- `18_Pulse_CoreS3_Demo/`
- `19_Pulse_CoreS3_Demo/`
- `20_Pulse_CoreS3_Demo/`
- `21_Pulse_CoreS3_Demo/`
- `22_Pulse_CoreS3_Demo/`
- `23_Pulse_CoreS3_Demo/`
- `24_Pulse_CoreS3_Demo/`
- `25_Pulse_CoreS3_Demo/`
- `26_Pulse_CoreS3_Demo/`
- `27_Pulse_CoreS3_Demo/`

## Folder format
Each version folder contains:

- the Arduino `.ino` sketch
- a `README.md` explaining the main changes for that version

## Current notes
- Later versions are closer to physical hardware testing.
- Confirm CoreS3 pinout, MOSFET input behavior, shared ground, flyback diode orientation, and output polarity before energizing any solenoid.
- SD logging direction: daily log files in `/logs/MM-DD-YYYY.log`; no master log.
- `stats.html` is intended as the visual parser/report for daily log files.
