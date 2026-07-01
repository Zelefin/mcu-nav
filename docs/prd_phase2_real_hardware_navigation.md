# PRD: Phase 2 Real Hardware Navigation

## Problem Statement

The project has a portable navigation core, replay path, simulator, control app,
mock peer source, NMEA parser, and initial ESP32 board bring-up code. The next
problem is proving that the navigation node can use real ESP32 hardware,
SX1280 ranging, GNSS input, and the control app to produce a real radio
navigation fallback instead of relying on `nav_mock`.

The first step cannot be parallelized. One person must produce a Working PoC
that passes the Hardware integration gate on the available boards. After that
PoC exists, contributors can independently choose the best next contribution
based on the project needs and their own knowledge.

## Solution

Create a serial first milestone that proves the available hardware end to end:

- two `esp32-s3-devkitc-1` nodes,
- two `nodemcu-32s` nodes,
- SX1280 initialization and ranging smoke evidence involving all four nodes,
- GNSS/NMEA input evidence from GPS-equipped nodes,
- control-app GPS disable on one node,
- trilateration fallback from real peer telemetry, fresh ranges, and local
  altitude.

The first firmware slice may be narrower: a Distance-only ranging PoC where
each node alternates between addressed slave listening and active master scans
to discover its own single-hop SX1280 range links without GNSS or a `RADIO_3D`
navigation solution. That slice provides radio-distance evidence but does not
replace the full Hardware integration gate.

Only after the Working PoC passes should the project open a contribution
backlog. That backlog is issue-shaped and independently grabbable, but it is not
assigned to fixed participants. Each issue states its prerequisites, acceptance
criteria, expected tests, and documentation updates.

The long-term outcome is an ESP32 navigation node where real driver data enters
the portable core through events, snapshots/logs leave the core, pair ranges are
visible in the control app, and a later NMEA output abstraction can emit a
GPS-like stream for an external consumer without owning flight-controller
integration.

## User Stories

1. As the project owner, I want one person to complete the Hardware integration
   gate before parallel work starts, so that the team does not build on
   unverified hardware assumptions.
2. As the project owner, I want a Working PoC on four ESP32 boards, so that the
   later backlog is grounded in a real end-to-end system.
3. As a hardware bring-up operator, I want every ESP32 board to flash and boot
   with visible self-test output, so that board-level problems are separated
   from navigation logic problems.
4. As a hardware bring-up operator, I want SX1280 initialization and ranging
   smoke evidence on the boards, so that radio capability is proven before TDMA
   integration work starts.
5. As a hardware bring-up operator, I want GNSS/NMEA input evidence from GPS
   boards, so that the ESP32 GNSS adapter can be built against proven wiring.
6. As a hardware bring-up operator, I want to disable GPS on one node from the
   control app, so that forced radio navigation can be verified through the same
   operator path used during demos.
7. As a navigation developer, I want the GPS-disabled node to fall back to radio
   navigation, so that GPS-denied behavior is proven on real boards.
8. As a navigation developer, I want real peer telemetry and real ranges to feed
   the portable core as events, so that the core remains deterministic and
   platform-independent.
9. As a navigation developer, I want local GNSS samples stamped with system time
   before injection, so that NMEA UTC time does not become the core event clock.
10. As a radio developer, I want telemetry slots to send encoded beacons from
    the local snapshot, so that peers build a real system view.
11. As a radio developer, I want received radio frames to decode into core
    events, so that the peer table is updated by real over-the-air data.
12. As a radio developer, I want SX1280 ranging slots to use the hardware
    ranging engine, so that distance is not estimated from RSSI, packet timing,
    or host round trips.
13. As a control-app user, I want to see pair ranges between nodes, so that I
    can diagnose network health from one connected node.
14. As a control-app user, I want third-party pair ranges to be displayed
    without making them local anchor distances, so that diagnostics do not
    corrupt the solver inputs.
15. As a replay user, I want endpoint-bearing range events in replay fixtures,
    so that hardware TDMA behavior can be reproduced without boards.
16. As a test author, I want `from_id`, `to_id`, `packet_seq`, `request_id`,
    `range_fail_reason`, and `reject_reason` to stay distinct, so that protocol,
    radio, and navigation failures remain diagnosable.
17. As a contributor, I want post-PoC issues with explicit prerequisites and
    acceptance criteria, so that I can choose a useful contribution
    independently.
18. As a contributor, I want optional SpeedyBee work to be clearly separate, so
    that ESP32 progress is not blocked by the ESP8285/Arduino target.
19. As a future output developer, I want an NMEA output abstraction after radio
    navigation is accepted, so that a GPS-like stream can be produced without
    putting flight-controller integration in this repository.
20. As a maintainer, I want docs, replay fixtures, and tests updated in the same
    changes as event or data-model changes, so that the repository remains
    replayable and internally consistent.

## Implementation Decisions

- The Hardware integration gate is a serial blocker. No parallel contribution
  lanes open until the Working PoC proves four ESP32 boards, SX1280 ranging
  smoke, GNSS/NMEA input, control-app GPS disable, and trilateration fallback.
- The Working PoC is not production readiness. It is the first verified
  end-to-end ESP32 setup that removes the largest hardware uncertainty.
- The supported main hardware path is two `esp32-s3-devkitc-1` nodes and two
  `nodemcu-32s` nodes. `speedybee` remains optional ranging debug work.
- The portable core remains C11 and platform independent. ESP-IDF, Arduino,
  FreeRTOS, UART, SPI, RadioLib, and board pin details stay in ports or platform
  code.
- The application continues to use event-in, snapshot/log-out flow. Normal
  application code injects events through the core API and reads snapshots
  rather than mutating core state directly.
- The GNSS adapter feeds UART bytes through the portable NMEA parser, stamps
  emitted samples with system time, and injects `NAV_EVT_LOCAL_GNSS_SAMPLE`.
- GPS disable from the control app must be observable in the core outcome. When
  GPS is disabled or forced-denied, local GNSS must not become the solution
  source.
- Telemetry TX uses the existing beacon encoder. Telemetry RX uses the existing
  decoder and injects decoded peer telemetry into the core.
- Ranging slots use the SX1280 ranging engine. RSSI/SNR are diagnostics only and
  must not be used as replacement distance measurements.
- Range result and failure payloads move to explicit `from_id` and `to_id`
  endpoints before hardware TDMA ranging is treated as the air/control contract.
- Local-endpoint ranges may update the existing anchor peer table. Third-party
  pair ranges are stored separately for network health, replay, logs, and the
  control app.
- `packet_seq` remains beacon sequence. `request_id` remains ranging attempt
  correlation. `range_fail_reason` remains radio-layer failure. `reject_reason`
  remains navigation-layer rejection.
- The validated trilateration behavior remains the source of truth for solver
  changes. This PRD does not require new navigation math.
- After the Working PoC, contributors pick from a backlog of issue-shaped
  vertical slices instead of being preassigned to fixed roles.
- NMEA output is a future output abstraction. It may emit GPS-like NMEA
  sentences from accepted snapshots, but flight-controller integration, MAVLink,
  and vehicle-specific behavior stay out of this repository.

## Testing Decisions

- The highest-value test seam is the externally visible event/snapshot/log path:
  replay inputs, decoded radio/GNSS events, control-app JSON, and hardware logs.
- Gate 0 acceptance requires a hardware evidence matrix: board identity, flash
  success, boot status, SX1280 init, per-node ranging participation,
  GNSS/NMEA evidence, control app GPS-disable evidence, and radio navigation
  fallback evidence.
- Host tests remain deterministic. Replay fixtures must be updated in the same
  change as event schema or data-model changes.
- NMEA parser behavior is tested byte by byte at the portable parser seam; the
  ESP32 adapter is tested by evidence that UART data becomes timestamped core
  samples.
- Telemetry and range codecs are tested by encode/decode round trips and by
  replay rows that exercise the same public contracts.
- Pair-range behavior is tested by local-endpoint and third-party endpoint
  cases. Third-party observations must be visible as network health and must not
  make peers usable as local anchors.
- Radio navigation acceptance is tested on real boards by disabling GPS through
  the control app and observing a `RADIO_3D` outcome from real peer telemetry,
  fresh SX1280 ranges, and local altitude.
- NMEA output is tested after radio navigation acceptance with deterministic
  sentence content and checksums, plus an ESP32 UART smoke test.

## Out of Scope

- Rewriting trilateration math.
- Adding platform dependencies to the portable core.
- ESP8285/SX1280 production radio firmware.
- Mesh or multi-hop networking.
- RSSI, SNR, packet timing, or host round-trip distance estimation.
- Flight-controller integration, MAVLink, PX4/ArduPilot setup, or vehicle state
  ownership.
- Simulator behavior inside replay.
- Treating `speedybee` as a blocker for the ESP32 Working PoC.
- Production RF calibration, antenna tuning, encryption, or authentication.

## Further Notes

- The issue backlog lives under `docs/issues/phase2_real_hardware_navigation/`.
- `0001-working-poc-hardware-integration-gate.md` is the serial blocker before
  independent contribution work starts.
- After the Working PoC, contributors should choose issues by value, dependency
  status, and personal fit rather than by preassigned lanes.
