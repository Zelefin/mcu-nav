# Simulation

The simulator will exercise the portable core without hardware.

## Baseline Scenario

- Four nodes.
- Three GPS-good anchors.
- One GPS-denied node.
- Target altitude is known from barometer or flight controller stand-in.
- Output streams: `events.csv`, `peers.csv`, `solution.csv`, and `truth.csv`.

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

The simulator should produce CSVs suitable for plotting:

- true vs estimated trajectory
- range residuals per anchor
- anchor quality over time
- accepted/rejected solution markers
- state machine timeline
