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
- Still future: config rows/sidecar config and Python-reference comparison.

## Milestone 4: Simulator

- Scenario format and generator.
- Noisy/bad measurements and packet loss.
- Visualization-ready CSV outputs.

## Milestone 5: Hardware Host Ports

- ESP32-S3 host adapter.
- STM32 host adapter.
- Keep all platform code under `ports/`.
