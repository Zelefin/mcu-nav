# Replay Plot Tool

`plot_replay.py` generates PNG diagnostics from replay outputs. It reads CSVs
written by `nav_replay`; it does not plot directly from simulator state.

Install the plotting dependency when needed:

```bash
python -m pip install matplotlib
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

The tool uses matplotlib's `Agg` backend for headless environments. If no
`RADIO_3D` rows exist, it writes placeholder diagnostic PNGs explaining that no
radio solution rows were available.
