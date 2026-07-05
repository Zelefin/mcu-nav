# ADR 0012: SX1280 2.4 GHz Time-of-Flight Ranging Instead of SX1262 Sub-GHz RSSI

## Status

**Accepted** (2026-07). Extends [ADR 0002](0002-radio-coprocessor-boundary.md)
(on-device SX1280 over SPI) and records the rationale for deviating from the
challenge-issued hardware kit.

## Context

The "Розробка комбінованої системи навігації" challenge brief prescribes a
specific kit: **STM32 F1/F4** MCUs, **LoRa SX1262** (sub-GHz) radios, and
distance obtained **"за рахунок вимірювання рівня отримуваного сигналу"** — i.e.
inter-node distance derived from **RSSI**.

Inter-node **distance** is the load-bearing input to the whole system: the
altitude-constrained least-squares trilateration solve
(`core/src/nav_trilateration.c`) is only as good as the ranges feeding it.
RSSI-to-distance conversion is notoriously imprecise: received power varies with
antenna orientation, multipath, body/obstacle shadowing, and per-unit hardware
spread, so a path-loss model typically yields errors of several metres to tens
of metres and needs constant environment-specific recalibration. That accuracy
floor is incompatible with a usable GPS-denied position fix.

## Decision

Use **Semtech SX1280 (2.4 GHz)** radios (E28-2G4M12SX modules; on-board SX1280
on SpeedyBee) and take distance from the **SX1280 hardware ranging engine**
(two-way / round-trip time-of-flight), **not** from RSSI. The host MCU is
**ESP32 / ESP32-S3** (ESP8285 on SpeedyBee), driving the SX1280 directly over
SPI via RadioLib — not STM32. RSSI and SNR are still recorded, but **only as
link-quality diagnostics**, never as a distance source (see
`docs/architecture.md`, `docs/radio_protocol.md`).

Rationale: the SX1280 is, to our knowledge, the readily available LoRa-class
part with a built-in ranging engine, so it measures distance in hardware instead
of inferring it from signal strength. This directly serves the challenge's core
goal (accurate ranging under GPS denial) better than the mandated approach. The
2026-07-04 field test bore this out: a GPS-denied node held a radio-navigation
fix off three GPS anchors with a **median residual RMS of ~3.8 m** — an order of
magnitude better than a realistic RSSI-based expectation.

## Considered options

- **SX1262 + RSSI path-loss model (as briefed).** Rejected: accuracy floor too
  high for a usable position solve; heavy, drift-prone per-site calibration.
- **SX1262 + custom sub-GHz time-of-flight.** Rejected: the SX1262 has no
  ranging engine, so this would mean building ToF/TDoA from scratch on tight
  radio timing — far more risk than using SX1280's hardware ranging.
- **UWB (e.g. DW1000).** Rejected: best ranging accuracy, but outside the issued
  kit and the LoRa-telemetry theme; would split telemetry and ranging across two
  radios.

## Consequences

- **Band trade-off (the honest downside):** 2.4 GHz has shorter range and worse
  obstacle penetration than SX1262 sub-GHz. Our validated geometry is
  short-range (tens to ~166 m). Long-range UAV separation is an open limitation,
  not a solved problem — call this out in any results claim.
- **Distance and telemetry share one radio and band**, so ranging and telemetry
  slots must be scheduled against each other (`core/src/nav_tdma.c`, ADR 0004);
  ranging always has priority.
- **We do not use the kit-provided GPS jammer or the exact uBlox part.** GPS
  denial is exercised in software (`{"cmd":"gps","enabled":false}`) and GNSS is a
  generic NMEA source (HGLRC M100-5883, [ADR 0006](0006-nmea-only-gnss-input.md)).
- Reverting to the briefed SX1262/RSSI stack would replace the radio, the band,
  the ranging method, and the driver layer — a whole-subsystem change, hence this
  record.
