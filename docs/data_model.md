# Data Model

## Node Ids

Node ids are `uint8_t` values from `0` to `NAV_MAX_NODES - 1`. The initial
system limit is four nodes. `NAV_INVALID_NODE_ID` is `0xFF`.

## Local Node

The local node id is `nav_config_t.local_node_id`. Local GNSS is stored in
`nav_system_t.local_gnss`. In `DEMO_FORCED_DENIED`, local GNSS position is
logged/debugged but not used as the navigation solution.

`nav_system_t` is public for static allocation, but the portable core owns its
contents. Normal application code should inject events and read snapshots rather
than mutating local GNSS, altitude, peer table, mode, or snapshot fields.

## Local Altitude

`nav_local_altitude_t` provides the altitude constraint required by the v1
three-anchor solver:

- `alt_mm`
- `timestamp_ms`
- `source`: `NONE`, `GNSS`, `BARO`, `FC`, `SIM`, or `MANUAL`
- `valid`

Forced-denied mode does not silently use GNSS altitude. GNSS altitude is allowed
only when `allow_gnss_altitude_in_demo_forced_denied` is true.

## Peer State

`nav_peer_state_t` stores one peer's telemetry, range, diagnostics, quality, last
residual, and last reject reason. Telemetry and range freshness are checked
against `telemetry_ttl_ms` and `range_ttl_ms`.

Beacon packet sequencing and ranging request correlation are separate:

- `packet_seq`: sequence number carried by peer beacon/telemetry packets.
- `last_range_request_id`: request/result correlation id for ranging attempts.

`nav_peer_beacon_rx_t` wraps `nav_peer_telemetry_t` plus local receive metadata:

- `telemetry`: what the remote peer reported.
- `rssi_dbm`: local RSSI for this received beacon.
- `snr_db`: local SNR in whole dB for v0.

RSSI/SNR are receive diagnostics, not peer-reported telemetry.

## Anchor Selection

`nav_anchor_t` is the compact solve input:

- `node_id`
- `lat_e7`, `lon_e7`, `alt_mm`
- `range_mm`, `range_sigma_mm`
- `quality`
- `reject_reason`

`nav_anchor_selection_t` contains selected anchors plus rejected node ids and
per-node reasons. V1 requires three usable anchors. If more than three exist,
the best three by quality are selected; overdetermined WLS is future work.

## Snapshot Diagnostics

`nav_snapshot_t` includes:

- `nav_mode`, `solution_status`, `reject_reason`
- `solution_source`: `NONE`, `LOCAL_GNSS`, `RADIO_3D`, `REPLAY`, or `SIM`
- output `position`
- selected anchor ids
- per-anchor residuals as `anchor_residuals_mm`
- `residual_rms_m`, `max_residual_m`
- `anchor_triangle_area_m2`, `geometry_score`
- rejected peer ids/reasons
- local altitude validity/source
- `total_quality`

## Config Thresholds

`nav_config_t` includes:

- `telemetry_ttl_ms`, `range_ttl_ms`, `local_altitude_ttl_ms`
- `max_range_sigma_mm`
- `min_anchor_quality`, `min_solution_quality`
- `max_residual_rms_m`, `max_residual_m`
- `min_anchor_triangle_area_m2`
- `degraded_anchor_triangle_area_m2`
- `demo_force_gps_denied`
- `allow_gnss_altitude_in_demo_forced_denied`

The geometry score is a v1 horizontal triangle-area heuristic, not full GDOP.

## Reject Reasons

Reject reasons explain the first blocking condition: stale telemetry, stale
range, bad peer GNSS, bad position, bad range sigma, range outlier, bad geometry,
not enough anchors, missing local altitude, or trilateration failure.

`NAV_REJECT_BAD_POSITION` is intentionally narrow. It means a peer marked GNSS
valid but supplied latitude outside `[-90, 90]` degrees, longitude outside
`[-180, 180]` degrees, or the default/missing position `{lat_e7=0, lon_e7=0,
alt_mm=0}`.

Radio/ranging failure causes use `nav_range_fail_reason_t`, not
`nav_reject_reason_t`. A radio timeout may later lead to `STALE_RANGE`, but the
two enums describe different layers.
