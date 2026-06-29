# Scenario Examples

These JSON files are deterministic simulator inputs for
`tools/sim/generate_scenario.py`. The generator writes replay inputs under
`build/generated/...`; generated CSV and PNG outputs are not committed.

Run one full pipeline:

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

Install plot dependencies first when needed:

```bash
python -m pip install -r requirements-dev.txt
```

Committed examples:

- `static_anchors_success.json`: fixed anchors and fixed blind node, perfect
  ranges, expected low-error `RADIO_3D`.
- `moving_blind_success.json`: fixed anchors and a slowly moving blind node,
  expected low-error `RADIO_3D`.
- `moving_anchors_success.json`: moving GPS-good anchors and a moving blind
  node, expected low-error `RADIO_3D`.
- `noisy_ranges_success.json`: seeded low-amplitude range noise with relaxed
  thresholds, expected deterministic `RADIO_3D`.
- `biased_range_degraded_or_rejected.json`: one biased peer range; current final
  behavior is `TRILATERATION_FAILED` after earlier diagnostic rows.
- `packet_loss_rejection.json`: early valid solves followed by deterministic
  range drops, expected final `NOT_ENOUGH_ANCHORS`.
- `degraded_geometry_rejection.json`: initially valid geometry degrades into a
  near-collinear final anchor triangle; expected final `BAD_GEOMETRY`.
- `stale_altitude_rejection.json`: one early local altitude sample expires by
  the final solve; expected final `MISSING_LOCAL_ALTITUDE`.
- `intermittent_solution_recovery.json`: middle steps lose ranges and final data
  recovers; expected final `RADIO_3D`.
- `forced_denied_with_local_gnss_debug.json`: emits local GNSS while
  forced-denied; expected final source remains `RADIO_3D`, not `LOCAL_GNSS`.
- `invalid_missing_nodes.json`: negative fixture for generator validation.
- `invalid_duplicate_node_id.json`: negative fixture for duplicate node ids.
- `invalid_missing_local_node.json`: negative fixture for missing local node id.
- `invalid_bad_packet_loss_mode.json`: negative fixture for packet-loss mode
  validation.

Aggregate final reject reasons can differ from the root cause for a specific
peer. Use generated `peers.csv` and `logs.txt` to inspect root causes such as
`STALE_RANGE`; use `solution.csv` for the final aggregate state.
