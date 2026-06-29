# Simulation

The canonical deterministic simulator path is `tools/sim/`. It generates replay
inputs and never bypasses `nav_replay`.

## Baseline Scenario

- Four nodes.
- Three GPS-good anchors.
- One GPS-denied node.
- Target altitude is known from barometer or flight controller stand-in.
- Generator outputs: `events.csv`, `truth.csv`, `replay_config.csv`, and
  `scenario_resolved.json`.
- Replay outputs: `solution.csv`, `peers.csv`, `logs.txt`, and
  `compare_report.txt/json`.

## Scenario Types

- Stationary anchors with stationary denied node.
- Stationary anchors with moving denied node.
- Moving anchors with moving denied node.
- Packet loss on telemetry or range messages.
- Noisy ranges.
- Fixed range bias.
- Range outliers.
- Bad or stale GNSS on one anchor.
- Poor horizontal geometry.

## Visualization Outputs

`tools/plot/plot_replay.py` reads replay outputs and writes:

- `trajectory_xy.png`
- `horizontal_error.png`
- `altitude_error.png`
- `residuals.png`
- `solution_quality.png`
- `plot_summary.json`

Install plotting dependencies with:

```bash
python -m pip install -r requirements-dev.txt
```
