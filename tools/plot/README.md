# Replay Plot Tool

`plot_replay.py` generates PNG diagnostics from replay outputs. It reads CSVs
written by `nav_replay`; it does not plot directly from simulator state.

Install the plotting dependency when needed:

```bash
python -m pip install -r requirements-dev.txt
```

Run:

```bash
python tools/plot/plot_replay.py \
  --truth build/generated/static_anchors_success/truth.csv \
  --solution build/generated/static_anchors_success/replay/solution.csv \
  --peers build/generated/static_anchors_success/replay/peers.csv \
  --compare-report build/generated/static_anchors_success/replay/compare_report.json \
  --out-dir build/generated/static_anchors_success/plots \
  --pretty
```

Outputs:

- `trajectory_xy.png`: truth and `RADIO_3D` estimate in local XY meters, with
  anchor positions when available from `peers.csv`.
- `horizontal_error.png`: horizontal error over time in meters.
- `altitude_error.png`: estimated minus true altitude over time in meters.
- `residuals.png`: residual0/1/2 over time in meters.
- `solution_quality.png`: `total_quality` and residual RMS over time.
- `plot_summary.json`: machine-readable row counts, comparable error metrics,
  residual maxima, and final solution/reject fields.

The tool uses matplotlib's `Agg` backend for headless environments. If no
`RADIO_3D` rows exist, it writes placeholder diagnostic PNGs explaining that no
radio solution rows were available.

`plot_summary.json` fields include:

- `rows_truth`, `rows_solution`, `rows_radio_solution`, `rows_compared`
- `rows_skipped_no_truth`, `rows_skipped_no_radio_solution`
- max/RMS horizontal, vertical, and 3D error metrics
- `max_residual_rms_m`, `max_abs_residual_m`
- `final_solution_status`, `final_solution_source`, `final_reject_reason`

Metrics that cannot be computed are written as JSON `null`. For intermittent
solutions, trajectory segments are split across long gaps and error plots include
only comparable `RADIO_3D` rows.
