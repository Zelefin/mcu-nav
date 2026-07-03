# ADR 0006: ESP32 GNSS Input Is NMEA-Only (UBX Parser Removed)

## Status

**Accepted** (2026-07-03). Extends [ADR 0005](0005-gps-denied-anchor-telemetry.md).

## Context

An exploratory u-blox UBX binary parser was added during GNSS bring-up
(`core/src/nav_ubx.c`, `core/include/nav/nav_ubx.h`, `docs/gnss_ubx.md`,
`tests/test_ubx_parser.c`). Its doc even declared that *"GPS-capable ESP32
targets use the u-blox UBX binary protocol only."* That contradicts:

- the Phase 2 PRD, whose GNSS adapter decision is *"feeds UART bytes through the
  portable NMEA parser"* (User Story 9), and
- the `CONTEXT.md` **Hardware integration gate** term, which requires
  *"GNSS/NMEA input evidence."*

The UBX files were never committed, never referenced by any build
(`tests/CMakeLists.txt`, `src/CMakeLists.txt`, `platformio.ini`), and never
wired into the ESP32 GNSS adapter (`src/GpsHealth.cpp` reads NMEA). So the
project had two conflicting GNSS directions, only one of which was ever built.

## Decision

ESP32 GNSS input is **NMEA-only**, via the portable `nav_nmea` parser and the
`GpsHealth` UART adapter. The UBX parser, its header, its doc, and its test are
removed. The `hacc_mm`, `vacc_mm`, and `velocity` fields that a UBX `NAV-PVT`
message would have populated remain unset from NMEA GGA/RMC input, consistent
with the existing NMEA parser documentation.

## Consequences

- `docs/gnss_nmea.md`'s limitation *"No u-blox UBX binary parser is
  implemented"* is accurate again and remains the single GNSS input doc.
- If precise horizontal/vertical accuracy or a GNSS velocity vector is later
  required on ESP32, reintroducing a UBX (or UBX+NMEA) path is a deliberate new
  decision, not a silent revert of this one.
