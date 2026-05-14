# Akizuki GNSS Notes

## Overview

This project targets the Akizuki GNSS module:

- Module: `GT-505GGBL5-DR-N`
- Board: `M5Stack Core2`
- Interface: `UART/TTL`

The current monitor program parses these NMEA sentences:

- `GGA`
- `GSA`
- `GSV`
- `RMC`

The purpose is to understand:

- current fix state
- current position
- satellites used for the navigation solution
- satellites visible to the receiver
- per-satellite signal strength, elevation, and azimuth

## Current Hardware Setup

The current working Core2-side UART setup is:

- `Serial2`
- `RX = GPIO13`
- `TX = GPIO14`
- `baudrate = 115200`

## Confirmed Module Facts

These points were confirmed from the module datasheet and actual behavior.

### Datasheet-based facts

- The module is a multi-GNSS receiver.
- Supported systems include:
  - `GPS`
  - `QZSS`
  - `GLONASS`
  - `Galileo`
  - `BeiDou`
  - `SBAS`
- Default update rate is `10 Hz`.
- Default UART baudrate is `115200 bps`.
- The module supports UART baudrates from `9600` to `961200 bps`.
- NMEA protocol version is listed as `NMEA 0183 Ver. 4.00 / 4.10`.

### NMEA field details confirmed from the datasheet

#### GGA

- `GGA` contains:
  - fix quality
  - latitude / longitude
  - HDOP
  - altitude
  - `Satellites Used`
- The datasheet states `Satellites Used` range is `00 ~ 56`.

#### GSA

- `GSA` contains:
  - fix type
  - the satellite IDs actually used for navigation
  - `PDOP`, `HDOP`, `VDOP`
  - `GNSS System ID`
- One `GSA` sentence can include at most `12` satellite IDs.
- The module may output multiple `GSA` sentences across multiple GNSS systems.
- The datasheet defines `GNSS System ID` as:
  - `1 = GPS`
  - `2 = GLONASS`
  - `3 = GALILEO`
  - `4 = BDS`

#### GSV

- `GSV` contains:
  - satellites in view
  - satellite ID
  - elevation
  - azimuth
  - `SNR / C/N0`
- `GSV` is split across multiple messages.
- One `GSV` message contains up to `4` satellites.

## Important Interpretation Notes

### "Used" and "Visible" are different

These must not be treated as the same thing.

- `Used`:
  satellites actually used in the navigation solution
- `Visible`:
  satellites that are currently visible / tracked / reported in `GSV`

The monitor distinguishes them as follows:

- `UsedGGA`
  - value taken directly from the `GGA` "Satellites Used" field
- `UsedGSA`
  - number of satellite IDs actually listed in `GSA`
- `Visible`
  - merged visible satellite count used by the display
- `GSVSeen`
  - visible count reported by `GSV`

### Current observed behavior on the Akizuki module

A key real-world observation from this module was:

- `UsedGGA = 41`
- `UsedGSA = 24`
- `Visible = GSVSeen`

This means:

- the `GSV` side of the parser is likely behaving reasonably
- the `GGA` "Satellites Used" value is not equivalent to the simple count of IDs seen in `GSA`

For practical interpretation, `UsedGSA` is currently the safer indicator of
"satellites explicitly listed as used by the module", while `UsedGGA` should
be treated as a module-reported aggregate value.

## Current Display / Serial Policy

The current monitor shows:

- fix type (`No Fix`, `2D Fix`, `3D Fix`)
- latitude / longitude / altitude
- `UsedGGA`
- `UsedGSA`
- `Visible`
- `GSVSeen`
- `HDOP`, `PDOP`, `VDOP`
- satellite table:
  - `SYS`
  - `ID`
  - `USE`
  - `CNO`
  - `EL`
  - `AZ`

Satellite list ordering is:

1. used satellites first
2. higher `C/N0` first
3. unused satellites after that

## Current Parsing Rules

### Checksum

- NMEA checksum validation is enabled.
- Sentences with invalid checksums are ignored.

### Talker / system handling

The code currently handles:

- `GP`
- `GL`
- `GA`
- `GB`
- `BD`
- `GQ`
- `GN`

Additional logic is used for:

- `QZSS` IDs around `193 ~ 199`

### GSA and GSV matching

The code currently:

1. builds visible satellites from `GSV`
2. builds used satellite references from `GSA`
3. marks `USE = *` if a `GSV` satellite is present in `GSA`

## What Is Considered Reliable Right Now

These items are considered reasonably trustworthy in the current implementation:

- fix type from `GSA`
- latitude / longitude from `GGA` / `RMC`
- altitude from `GGA`
- visible satellite table from `GSV`
- `UsedGSA` count
- per-satellite `USE` mark based on `GSA` vs `GSV`

These items should be interpreted more carefully:

- `UsedGGA`
  - module-reported aggregate value
  - may not match the explicit count of IDs listed in `GSA`

## Open Questions / Future Checks

The following are still worth checking later:

- whether the module emits multiple `GSA` sentences per epoch for different systems
- whether `GNGSV` mixes multiple systems in ways that require additional separation logic
- whether the module's `GGA` "Satellites Used" field includes a broader internal count than `GSA`
- whether logging raw NMEA during a stable outdoor session clarifies the `UsedGGA` vs `UsedGSA` gap

## Repository Notes

When publishing this project to GitHub, it would be useful to keep:

- this note file
- the datasheet filename reference:
  - `YIC-GT-505GGBL5-DR.pdf`
- a short wiring note in the main README
- a statement that the current verified UART setting is `115200 bps`

