# Development Plan

## Milestone 1: Foundation

- Portable C core layout.
- CMake host build and tests.
- Peer table, quality, state helpers, logging/radio/replay skeletons.
- Reference-derived trilateration adapter and deterministic sample test.
- Documentation and ADRs.

## Milestone 2: Radio Navigation Path

- Done for the portable core: select three anchors, require local altitude, call
  `nav_trilat_solve_3_anchor_altitude`, populate snapshot residuals/quality, and
  emit structured decision logs.

## Milestone 3: Replay

- Done: host `nav_replay` parses deterministic `events.csv` fixtures, drives
  `nav_core_tick()` and `nav_core_handle_event()`, captures structured logs, and
  emits `solution.csv`, `peers.csv`, and `logs.txt`.
- Done: CTest replay fixtures cover successful radio 3D navigation, rejection
  cases, and malformed parser input.
- Done: fixture-level `replay_config.csv`, optional `truth.csv`, final expected
  status checks, `compare_report.txt/json`, and optional Python reference
  comparison helper.
- Still future: full simulator-generated scenarios and broad reference
  comparison coverage.

## Milestone 4: Simulator

- Done: JSON scenario format and deterministic generator under `tools/sim/`.
- Done: generated `events.csv`, `truth.csv`, `replay_config.csv`, and
  `scenario_resolved.json`.
- Done: optional generator `--run-replay` path that invokes existing
  `nav_replay` and writes outputs under `replay/`.
- Done: deterministic seeded range noise, static range bias, explicit packet
  drops, fixed/moving blind nodes, and slow moving anchors.
- Done: plotting tool under `tools/plot/` that creates PNG diagnostics from
  replay outputs.
- Done: edge-case scenarios for degraded geometry, stale altitude, intermittent
  recovery, and forced-denied local GNSS debug.
- Done: plot summaries in `plot_summary.json`.
- Future: longer moving-anchor cases, overdetermined anchor sets after the core
  supports them, and richer plot summaries.

## Milestone 5: GNSS/NMEA Parser

- Done: portable byte-by-byte NMEA parser under `core/`.
- Done: checksum validation, fixed sentence buffer, no dynamic allocation, and
  support for `$GPGGA`/`$GNGGA` plus `$GPRMC`/`$GNRMC`.
- Done: parser output maps to `nav_gnss_sample_t` and can be injected through
  `NAV_EVT_LOCAL_GNSS_SAMPLE`.
- Done: host tests cover valid/invalid fixes, malformed input, checksum errors,
  stream recovery, overlong sentences, direct GNSS solution, and forced-denied
  behavior.
- Done: ESP32 platform UART adapter stamps portable NMEA samples with system
  time and injects `NAV_EVT_LOCAL_GNSS_SAMPLE`.
- Future: UTC/PPS time handling, UBX parsing if needed, and velocity derivation
  from RMC.

## Milestone 6: ESP32 Hardware Integration

Current PRD and issue-shaped backlog:

- `docs/prd_phase2_real_hardware_navigation.md`
- `docs/issues/phase2_real_hardware_navigation/README.md`

- Done: ESP-IDF GNSS adapter that stamps portable NMEA samples with system time
  and injects `NAV_EVT_LOCAL_GNSS_SAMPLE`.
- Partial: ESP32 radio task sends best-effort GPS-valid telemetry beacons and
  receives `nav_telemetry` beacon frames during guarded packet windows.
- Future: ESP32 radio task that runs the full TDMA scheduler.
- Future: scheduled telemetry slots use normal SX1280 packet TX/RX and
  `nav_telemetry` frames.
- Ranging slots use the SX1280 ranging engine, following the proven
  `examples/esp32s3-ranging` RadioLib workflow.
- Successful ranging-engine results are injected as `NAV_EVT_RANGE_RESULT`;
  failures are injected as `NAV_EVT_RANGE_FAIL`.
- Range result/failure payloads move from implicit local `peer_id` semantics to
  explicit `from_id` / `to_id` endpoint pairs so every node can record
  third-party pair ranges for network health and the telemetry UI.
- Local-endpoint ranges update the anchor peer table; third-party pair ranges
  are stored separately until the solver explicitly supports inter-peer
  constraints.
- Keep all platform code out of `core/`.
