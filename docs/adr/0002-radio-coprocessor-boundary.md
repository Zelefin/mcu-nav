# ADR 0002: Radio Coprocessor Boundary

## Status

**Superseded** (2026-07) by the on-device radio decision. See "Superseding
decision" below. Kept for historical context.

## Original decision

This repository defines only the host-side contract for the radio/ranging
coprocessor. ESP8285/SX1280 firmware is implemented in a separate repository.

## Original rationale

Navigation decisions and radio PHY/MAC behavior have different test loops,
hardware requirements, and ownership boundaries.

## Superseding decision

The selected hardware drives the SX1280 directly from the ESP over SPI (E28
module on the ESP32 boards, on-board SX1280 on SpeedyBee). There is no separate
coprocessor MCU. Therefore:

- TDMA scheduling, ranging, and telemetry run on the same MCU as the navigation
  core, not behind a UART contract.
- `nav_radio_protocol` is reused as the **over-the-air packet format between
  nodes** instead of a host<->coprocessor framing.
- The portable `core/` still receives radio information as `nav_event_t`; only
  the source changed from "coprocessor UART frames" to "on-device radio driver".

The portable core remains free of ESP-IDF/Arduino/RTOS dependencies. Driver and
radio code live in `ports/<platform>`.

## Consequences

`core/` receives radio information as events. Packet payloads and framing evolve
here and are shared by every node over the air. See `docs/radio_protocol.md`.
