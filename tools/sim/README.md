# Scenario Generator

`generate_scenario.py` turns deterministic software scenarios into replay input
files. It does not call `nav_core` directly for the simulation result.

```text
scenario.json
    -> generate_scenario.py
    -> events.csv + truth.csv + replay_config.csv
    -> nav_replay
```

Generate a scenario:

```bash
python tools/sim/generate_scenario.py \
  --scenario examples/scenarios/static_anchors_success.json \
  --out-dir build/generated/static_anchors_success \
  --overwrite \
  --pretty
```

Generate and immediately run replay:

```bash
python tools/sim/generate_scenario.py \
  --scenario examples/scenarios/static_anchors_success.json \
  --out-dir build/generated/static_anchors_success \
  --run-replay ./build/tools/replay/nav_replay \
  --overwrite \
  --pretty
```

Outputs:

- `events.csv`: deterministic replay events.
- `truth.csv`: local/blind-node truth at each scenario step.
- `replay_config.csv`: replay thresholds and expected final status.
- `scenario_resolved.json`: parsed scenario plus defaults and derived local
  coordinates for debugging.
- `replay/`: optional `nav_replay` output when `--run-replay` is used.

The generator supports linear local motion, valid/invalid GNSS flags, forced
GPS-denied local nodes, seeded Gaussian range noise, static per-peer range bias,
simple packet-loss patterns, and deterministic local-altitude emission cadence.
Coordinates are generated from a local tangent approximation around the scenario
origin; ranges are generated from WGS84 ECEF positions derived from the emitted
lat/lon/alt rows.

The canonical simulator directory is `tools/sim/`.

Add a new scenario by:

1. Creating `examples/scenarios/<name>.json`.
2. Setting `replay_config.expect_final_*` fields for deterministic replay
   validation.
3. Running `python tools/sim/generate_scenario.py --scenario ... --out-dir
   build/generated/<name> --run-replay ./build/tools/replay/nav_replay
   --overwrite --pretty`.
4. Adding CTest coverage in `tests/CMakeLists.txt` when the scenario should be
   part of regression coverage.

Current limitations:

- JSON only; no YAML dependency is required.
- Exactly one blind/local node is supported.
- At least three anchors are required when a scenario expects a final
  `RADIO_3D` solution.
- No random Monte Carlo framework; any noise must use an explicit seed.
- No hardware, GNSS/NMEA parser, radio firmware, SPI driver, MAVLink, GUI, or
  dashboard behavior is implemented here.

See [scenario_schema.md](scenario_schema.md) for the JSON schema.
