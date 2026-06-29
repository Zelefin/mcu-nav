# Debug Playbook

## Why Is There No RADIO_3D Solution?

Inspect the latest `nav_snapshot_t.reject_reason` or `SOLUTION solve_rejected`
log:

- `NOT_ENOUGH_ANCHORS`: fewer than three peers passed anchor selection.
- `MISSING_LOCAL_ALTITUDE`: no fresh valid local altitude sample was available.
- `BAD_GEOMETRY`: accepted anchors had too little horizontal triangle area.
- `TRILATERATION_FAILED`: the reference-derived C solver did not converge.
- `RANGE_OUTLIER`: residual thresholds were exceeded after a returned solve.
- `BAD_POSITION`: peer GNSS was marked valid, but the peer position was outside
  valid WGS84 lat/lon bounds or still the default `{0,0,0}` placeholder.

Run `ctest --test-dir build --output-on-failure -R test_radio_navigation` for
the deterministic radio-navigation cases.

For file-driven debugging, run:

```bash
./build/tools/replay/nav_replay \
  --events examples/replay/radio_3d_success/events.csv \
  --out-dir build/replay/radio_3d_success \
  --node-id 0 \
  --pretty
```

Then inspect `solution.csv` for the final status, `peers.csv` for anchor state,
`logs.txt` for the exact rejection or solve decision, and `compare_report.txt`
when truth comparison is enabled.

## How Do I Know Local Altitude Is Valid?

Look for:

```text
cat=GNSS event=local_altitude_accepted valid=1 source=SIM alt_mm=183500
```

The snapshot also reports `local_altitude_valid` and `altitude_source`. In
forced-denied mode, GNSS altitude is not used unless
`allow_gnss_altitude_in_demo_forced_denied` is explicitly true.

## How Do I Know Which Anchor Was Rejected?

Check `QUALITY anchor_rejected` logs and `nav_snapshot_t.rejected_node_ids` with
`rejected_reasons`. The peer table also stores `last_reject_reason`.

## How Do I Inspect Residuals?

For accepted solutions, inspect:

- `nav_snapshot_t.anchor_residuals_mm`
- `nav_snapshot_t.residual_rms_m`
- `nav_snapshot_t.max_residual_m`
- `SOLUTION residual` logs
- replay `solution.csv` columns `residual0_mm`, `residual1_mm`,
  `residual2_mm`, `residual_rms_m`, and `max_residual_m`

Biased inconsistent ranges may cause `TRILATERATION_FAILED` before residual
threshold logic runs; this reflects the current portable C solver behavior.

## Stale Telemetry Vs Stale Range

- `STALE_TELEMETRY`: no fresh peer position/GNSS data.
- `STALE_RANGE`: peer telemetry may be fresh, but the TWR range is missing,
  invalid, or older than `range_ttl_ms`.

`PEER_TABLE peer_marked_stale` includes both telemetry and range ages.

## Is Forced-Denied Using Local GNSS Position?

It should not. In forced-denied mode, `local_gnss_sample` may appear in logs, but
the final `solution_status` should be `RADIO_3D` or `REJECTED`, not
`GNSS_DIRECT`, and `solution_source` must not be `LOCAL_GNSS`. Test
`test_forced_denied_ignores_local_gnss_position` verifies this.

In replay, check `solution.csv` column `solution_source`. The success fixture
must end with `RADIO_3D`, not `LOCAL_GNSS`.

## How Do I Debug A Failed Replay?

1. Confirm `nav_replay` exited nonzero only for malformed input.
2. Read the final `solution.csv` row: `reject_reason` is the navigation-level
   decision.
3. Read `peers.csv`: stale telemetry and stale range are separated by
   `telemetry_age_ms`, `range_age_ms`, and each peer `reject_reason`.
4. Search `logs.txt` for `anchor_rejected`, `solve_rejected`, or parser error
   lines with input line numbers.
5. Run `ctest --test-dir build -V -R replay` to compare against regression
   fixtures.

If `compare_report.txt` says `pass=false`, check the max error fields against
the `max_allowed_*_error_m` thresholds in `replay_config.csv`. Rows without
`RADIO_3D` source are counted as `rows_skipped_no_radio_solution`; radio
solutions without exact truth timestamps are counted as `rows_skipped_no_truth`.

## How Do I Debug A Generated Scenario?

Generate with `--pretty` and inspect `scenario_resolved.json` first. It contains
the parsed defaults, node ids, local origin, range model, packet-loss rules, and
replay config that produced the CSV files.

Then run the generated files through replay:

```bash
python tools/sim/generate_scenario.py \
  --scenario examples/scenarios/static_anchors_success.json \
  --out-dir build/generated/static_anchors_success \
  --run-replay ./build/tools/replay/nav_replay \
  --overwrite \
  --pretty
```

If replay passes but the trajectory looks suspicious, generate plots from replay
outputs:

```bash
python tools/plot/plot_replay.py \
  --truth build/generated/static_anchors_success/truth.csv \
  --solution build/generated/static_anchors_success/replay/solution.csv \
  --peers build/generated/static_anchors_success/replay/peers.csv \
  --compare-report build/generated/static_anchors_success/replay/compare_report.json \
  --out-dir build/generated/static_anchors_success/plots \
  --pretty
```

Use `trajectory_xy.png` for geometry mistakes, `horizontal_error.png` and
`altitude_error.png` for truth mismatch, `residuals.png` for inconsistent
ranges, and `solution_quality.png` for acceptance/quality trends.
`plot_summary.json` contains row counts, comparable error metrics, residual
maxima, and final solution/reject fields for scripts.

For intermittent scenarios, error plots include only comparable `RADIO_3D` rows.
The trajectory plot splits estimated segments across long solution gaps so a
missing solution interval is not drawn as continuous navigation. Inspect
`rows_radio_solution`, `rows_skipped_no_radio_solution`, `peers.csv`, and
`logs.txt` to separate aggregate final reject reasons from root causes such as
`STALE_RANGE`, `BAD_GEOMETRY`, or `MISSING_LOCAL_ALTITUDE`.

## Radio Failure Vs Navigation Rejection

`range_fail_reason` explains why the radio/ranging attempt failed, such as
`TIMEOUT` or `RADIO_BUSY`. `reject_reason` explains why the navigation core did
not use an anchor or solution, such as `STALE_RANGE` or `BAD_POSITION`.

## Which Test Covers What?

- `test_radio_3d_solution_exact`: exact three-anchor radio solve.
- `test_radio_3d_reject_not_enough_anchors`: only two usable anchors.
- `test_radio_3d_reject_stale_telemetry`: stale peer telemetry.
- `test_radio_3d_reject_stale_range`: stale range.
- `test_radio_3d_reject_bad_peer_gnss`: invalid peer GNSS.
- `test_radio_3d_reject_missing_altitude`: no local altitude.
- `test_forced_denied_ignores_local_gnss_position`: GNSS is not used as position
  in forced-denied mode.
- `test_radio_solution_residual_rejected`: biased range currently rejects via
  trilateration no-convergence.
- `test_beacon_rx_metadata_reaches_peer_diagnostics`: RSSI/SNR stay separate
  from peer telemetry and reach peer diagnostics.
- `test_packet_seq_and_request_id_are_not_mixed`: beacon packet sequence and
  ranging request id remain separate.
- `test_range_failure_uses_range_fail_reason`: range failures use radio-layer
  reasons, not navigation reject reasons.
- `test_radio_3d_reject_bad_position`: invalid peer lat/lon rejects as
  `BAD_POSITION`.
- `replay_radio_3d_success`: success fixture produces `RADIO_NAV_OK`,
  `RADIO_3D`, source `RADIO_3D`, and low residuals.
- `replay_reject_*`: replay rejection fixtures preserve deterministic reject
  behavior.
- `replay_parser_*`: malformed replay input is rejected with nonzero exit.
- `replay_parser_invalid_config_*`: malformed `replay_config.csv` is rejected.
- `replay_parser_invalid_truth_*`: malformed `truth.csv` is rejected.
- `scenario_validate_*`: committed scenario JSON files generate replay inputs.
- `scenario_replay_*`: generated success scenarios pass through `nav_replay`.
- `scenario_invalid_*`: malformed scenario JSON fails before CSV generation.
- `plot_*`: generated replay outputs produce PNG plots and `plot_summary.json`.
