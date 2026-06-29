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
  `solution.csv`, `peers.csv`, `logs.txt`, and optional truth comparison
  reports for regression/debug.
- Deterministic scenario generator produces `events.csv`, `truth.csv`, and
  `replay_config.csv` from JSON scenario files.
- Plotting tool generates replay diagnostics PNGs from `truth.csv`,
  `solution.csv`, `peers.csv`, and `compare_report.json`.
- Real hardware ports, radio firmware, GNSS parser, and FC/MAVLink output remain
  future work.

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
  --truth examples/replay/radio_3d_success/truth.csv \
  --config examples/replay/radio_3d_success/replay_config.csv \
  --out-dir build/replay/radio_3d_success \
  --node-id 0 \
  --pretty
```

Replay auto-discovers `replay_config.csv` and `truth.csv` next to `events.csv`
unless disabled. It writes `solution.csv`, `peers.csv`, `logs.txt`, and, when
truth is present, `compare_report.txt` and `compare_report.json`. Run only
replay tests with:

```bash
ctest --test-dir build -V -R replay
```

Generate, replay, and plot a deterministic software scenario:

```bash
python tools/sim/generate_scenario.py \
  --scenario examples/scenarios/static_anchors_success.json \
  --out-dir build/generated/static_anchors_success \
  --run-replay ./build/tools/replay/nav_replay \
  --overwrite \
  --pretty

python tools/plot/plot_replay.py \
  --truth build/generated/static_anchors_success/truth.csv \
  --solution build/generated/static_anchors_success/replay/solution.csv \
  --peers build/generated/static_anchors_success/replay/peers.csv \
  --compare-report build/generated/static_anchors_success/replay/compare_report.json \
  --out-dir build/generated/static_anchors_success/plots \
  --pretty
```

The plot tool requires matplotlib:

```bash
python -m pip install matplotlib
```

## Repository Structure

```text
core/            Portable C11 navigation core and public headers.
ports/           Platform adapters. Only POSIX demo exists now.
docs/            Architecture, data model, logging, replay, and protocol docs.
tools/replay/    Deterministic CSV replay runner.
tools/sim/       Deterministic scenario-to-replay-input generator.
tools/plot/      Replay-output PNG diagnostics.
tests/           Host C tests for implemented core modules.
examples/replay/ Deterministic replay fixtures.
examples/scenarios/ Committed deterministic scenario definitions.
examples/        Captured log examples and scenario/replay fixtures.
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
  `RADIO_NAV_OK`, `RADIO_3D`, source `RADIO_3D`, reject `NONE`; includes
  `truth.csv` and comparison thresholds.
- `examples/replay/reject_not_enough_anchors/events.csv`: expected final
  `NOT_ENOUGH_ANCHORS`.
- `examples/replay/reject_missing_altitude/events.csv`: expected final
  `MISSING_LOCAL_ALTITUDE`.
- `examples/replay/reject_stale_range/events.csv`: expected final
  `NOT_ENOUGH_ANCHORS`, with logs showing `STALE_RANGE`.
- `examples/replay/reject_bad_position/events.csv`: expected final
  `NOT_ENOUGH_ANCHORS`, with logs showing `BAD_POSITION`.

## Next Milestones

1. Expand scenario coverage for degraded geometry, bad altitude, and longer
   moving-anchor runs.
2. Add more replay fixtures for degraded geometry, biased ranges, and stale
   local altitude.
3. Add richer plot diagnostics and machine-readable plot summaries.
4. Implement full radio protocol framing with COBS, CRC32, ACKs, and timeouts.
5. Add ESP32-S3/STM32 host adapters without platform dependencies in `core/`.
