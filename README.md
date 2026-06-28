# nav-mcu

Portable navigation-brain firmware for a group UAV navigation system.

This repository owns the main MCU navigation core, host tests, diagnostics,
documentation, and future host ports. It does not implement the ESP8285/SX1280
radio firmware, real GNSS drivers, a full simulator, or flight-controller output.

## Current Status

- C11 portable `nav_core` static library builds on Linux.
- Event-in, snapshot/log-out architecture is implemented.
- Peer telemetry and range results update a deterministic peer table.
- Forced GPS-denied mode can solve a `RADIO_3D` position from three fresh
  GPS-good peer anchors, three ranges, and a valid local altitude sample.
- Snapshot diagnostics include an explicit solution source, so forced-denied
  debug can prove whether a solution came from local GNSS or radio ranging.
- The trilateration adapter is ported from
  `~/projects/trilateration/mcu/esp32_s3_demo/components/trilat`.
- Structured callback logs explain anchor selection, rejections, solve attempts,
  solve success, residuals, and mode/status transitions.
- POSIX demo injects a deterministic forced-denied scenario and prints the
  resulting snapshot.
- Replay CLI consumes deterministic `events.csv` fixtures and writes
  `solution.csv`, `peers.csv`, and `logs.txt` for regression/debug.
- Full simulator, plotting, real hardware ports, radio firmware, and
  FC/MAVLink output remain future work.

## Build And Test

```bash
cmake -S . -B build
cmake --build build
ctest --test-dir build --output-on-failure
```

Run the host demo:

```bash
./build/ports/posix/nav_posix_demo
```

Run a replay fixture:

```bash
./build/tools/replay/nav_replay \
  --events examples/replay/radio_3d_success/events.csv \
  --out-dir build/replay/radio_3d_success \
  --node-id 0 \
  --pretty
```

Replay writes `solution.csv`, `peers.csv`, and `logs.txt` in the output
directory. Run only replay tests with:

```bash
ctest --test-dir build -V -R replay
```

## Repository Structure

```text
core/            Portable C11 navigation core and public headers.
ports/           Platform adapters. Only POSIX demo exists now.
docs/            Architecture, data model, logging, replay, and protocol docs.
tools/replay/    Deterministic CSV replay runner.
tools/           Future simulator and plot tools.
tests/           Host C tests for implemented core modules.
examples/replay/ Deterministic replay fixtures.
examples/        Future scenarios and captured log examples.
```

## Read First

- [Architecture](docs/architecture.md)
- [Data flow](docs/data_flow.md)
- [Data model](docs/data_model.md)
- [State machine](docs/state_machine.md)
- [Logging](docs/logging.md)
- [Debug playbook](docs/debug_playbook.md)
- [Radio protocol](docs/radio_protocol.md)
- [Replay CSV](docs/replay_csv.md)

## Replay Fixtures

- `examples/replay/radio_3d_success/events.csv`: expected final
  `RADIO_NAV_OK`, `RADIO_3D`, source `RADIO_3D`, reject `NONE`.
- `examples/replay/reject_not_enough_anchors/events.csv`: expected final
  `NOT_ENOUGH_ANCHORS`.
- `examples/replay/reject_missing_altitude/events.csv`: expected final
  `MISSING_LOCAL_ALTITUDE`.
- `examples/replay/reject_stale_range/events.csv`: expected final
  `NOT_ENOUGH_ANCHORS`, with logs showing `STALE_RANGE`.
- `examples/replay/reject_bad_position/events.csv`: expected final
  `NOT_ENOUGH_ANCHORS`, with logs showing `BAD_POSITION`.

## Next Milestones

1. Add replay config rows or a sidecar config file for threshold sweeps.
2. Add Python-reference comparison for selected replay fixtures.
3. Add simulator scenario loading and visualization-ready outputs.
4. Implement full radio protocol framing with COBS, CRC32, ACKs, and timeouts.
5. Add ESP32-S3/STM32 host adapters without platform dependencies in `core/`.
