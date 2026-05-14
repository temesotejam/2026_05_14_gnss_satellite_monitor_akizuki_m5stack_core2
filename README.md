# GNSS Satellite Monitor for Akizuki GT-505 on M5Stack Core2

## Overview

This PlatformIO project monitors an Akizuki `GT-505GGBL5-DR-N` GNSS module from an `M5Stack Core2`.

It receives NMEA data over UART and parses:

- `GGA`
- `GSA`
- `GSV`
- `RMC`

The display and Serial Monitor are used to inspect:

- current fix type
- latitude / longitude / altitude
- DOP values
- satellites used for navigation
- satellites visible to the receiver
- per-satellite C/N0, elevation, and azimuth

## Hardware

- Board: `M5Stack Core2`
- GNSS module: `GT-505GGBL5-DR-N`
- UART: `Serial2`
- RX pin: `GPIO13`
- TX pin: `GPIO14`
- Verified baudrate: `115200`

## Why This Project Exists

This project was created to compare two GNSS configurations:

1. `M5Stack Core2 + Akizuki GT-505GGBL5-DR-N`
2. `M5Stack CoreS3 + M5Stack GNSS Module / u-blox NEO-M9N`

The focus here is not only position output, but also GNSS satellite behavior:

- which satellites are visible
- which satellites are used
- how `GGA`, `GSA`, and `GSV` differ

## Current Status

The project currently parses and displays:

- Fix type from `GSA`
- Position from `GGA` / `RMC`
- Altitude from `GGA`
- `UsedGGA`
- `UsedGSA`
- `Visible`
- `GSVSeen`
- `HDOP`, `PDOP`, `VDOP`
- satellite table with:
  - `SYS`
  - `ID`
  - `USE`
  - `CNO`
  - `EL`
  - `AZ`

## Important Observation

On this module, `UsedGGA` and `UsedGSA` do not currently match.

Example observed values:

- `UsedGGA = 41`
- `UsedGSA = 24`
- `Visible = GSVSeen`

This suggests:

- `GSV` parsing is probably reasonable
- the `GGA` satellite count is not equivalent to a simple count of IDs found in `GSA`

For now, `UsedGSA` is the more practical value when interpreting "satellites explicitly listed as used".

## Build

This is a PlatformIO project.

Typical commands:

```powershell
platformio run
platformio run --target upload
platformio device monitor -b 115200
```

## Notes

Detailed investigation notes are kept here:

- [AKIZUKI_GNSS_NOTES.md](./AKIZUKI_GNSS_NOTES.md)

## Repository Scope

This repository is intended to keep:

- source code
- PlatformIO project files
- markdown notes

It does not keep build output from `.pio/`.
