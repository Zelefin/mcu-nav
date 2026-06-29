# Scenario JSON Schema

The scenario generator accepts deterministic JSON files. Unknown top-level
fields are preserved only in the source file; unknown `replay_config` keys are
rejected because `nav_replay` rejects them too.

## Top-Level Fields

- `scenario_name`: non-empty string.
- `local_node_id`: integer `0..3`; must identify the blind node.
- `start_time_ms`, `end_time_ms`, `step_ms`: integer timeline. `end_time_ms` is
  inclusive.
- `origin_lat_e7`, `origin_lon_e7`, `origin_alt_mm`: local reference point.
- `nodes`: array of one blind node and one or more anchor nodes.
- `range_model`: optional range generation settings.
- `packet_loss`: optional deterministic drop settings.
- `replay_config`: optional replay thresholds and expected final status.
- `output_dir`: optional note; the CLI `--out-dir` controls actual output.

## Nodes

Each node uses explicit units:

```json
{
  "node_id": 1,
  "role": "anchor",
  "initial_position": {
    "lat_e7": 504560441,
    "lon_e7": 305218620,
    "alt_mm": 180000
  },
  "velocity": {
    "north_mmps": 0,
    "east_mmps": 0,
    "down_mmps": 0
  },
  "gnss_valid": true,
  "nav_mode": "GNSS_OK"
}
```

Supported `role` values are `anchor` and `blind`. The blind node must be the
`local_node_id`. Anchor `nav_mode` is normally `GNSS_OK`; blind scenarios use
`DEMO_FORCED_DENIED` with replay config `demo_force_gps_denied=true`.

Optional node diagnostics default to replay-friendly values:

- `fix_type`: default `3D`.
- `satellites`: default `12`.
- `hdop_centi`: default `80`.
- `hacc_mm`: default `1000`.
- `vacc_mm`: default `1500`.
- `emit_local_gnss`: default `false`; when true, local GNSS samples are emitted
  but forced-denied replay still must not use them as the solution source.

## Range Model

```json
{
  "enabled": true,
  "noise_std_m": 0.0,
  "bias_by_peer_m": {"1": 0.0, "2": 0.0, "3": 0.0},
  "seed": 12345,
  "range_sigma_mm": 100
}
```

Ranges are true 3D WGS84 ECEF distances between emitted node positions plus
configured bias and seeded Gaussian noise. Noise is deterministic for a given
seed and row order.

## Packet Loss

Supported modes:

- `none`: no drops.
- `drop_every_n`: drops every nth generated `RANGE_RESULT`.
- `explicit`: drops listed events.

Example:

```json
{
  "mode": "explicit",
  "explicit_drops": [
    {"time_ms": 3000, "peer_id": 3, "event_type": "RANGE_RESULT"}
  ]
}
```

`event_type` may be `RANGE_RESULT` or `PEER_BEACON_RX`.

## Replay Config

`replay_config` is written directly to `replay_config.csv` after defaults are
applied. Supported keys match `docs/replay_csv.md`, including:

- freshness and quality thresholds such as `telemetry_ttl_ms`,
  `range_ttl_ms`, `local_altitude_ttl_ms`, `max_range_sigma_mm`,
  `max_residual_rms_m`, `max_residual_m`,
  `min_anchor_triangle_area_m2`, and
  `degraded_anchor_triangle_area_m2`.
- truth comparison thresholds:
  `max_allowed_horizontal_error_m`, `max_allowed_vertical_error_m`,
  `max_allowed_3d_error_m`.
- expected final values:
  `expect_final_mode`, `expect_final_solution`, `expect_final_source`,
  `expect_final_reject`.

Defaults are radio-navigation oriented: local node id from the scenario,
`demo_force_gps_denied=true`, TTLs of `1500 ms`, and `1.0 m` truth comparison
limits.

## Generated Event Order

For each time step, rows are emitted deterministically:

```text
TICK
LOCAL_ALTITUDE_SAMPLE
optional LOCAL_GNSS_SAMPLE
PEER_BEACON_RX for each anchor by peer id
RANGE_RESULT for each anchor by peer id unless dropped
TICK
```

`packet_seq` is generated for beacons and `request_id` is generated for ranges;
the names intentionally match the radio protocol contract.

## Approximation Notes

Node motion is linear in a local north/east/down frame around the origin.
Generated lat/lon uses an equirectangular approximation, which is suitable for
small replay scenarios near the origin. Ranges are generated from WGS84 ECEF
coordinates derived from those emitted positions, so replay and plotting consume
the same CSV truth that was used to produce measurements.
