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

## Milestone 5: Hardware Host Ports

- ESP32-S3 host adapter.
- STM32 host adapter.
- Keep all platform code under `ports/`.
