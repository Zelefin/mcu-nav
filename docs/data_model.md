# Data Model

## Node Ids

Node ids are `uint8_t` values from `0` to `NAV_MAX_NODES - 1`. The initial
system limit is four nodes. `NAV_INVALID_NODE_ID` is `0xFF`.

## Local Node

The local node id is `nav_config_t.local_node_id`. Local GNSS is stored in
`nav_system_t.local_gnss`. In `DEMO_FORCED_DENIED`, local GNSS position is
logged/debugged but not used as the navigation solution.

`nav_gnss_sample_t` is also the output type for the portable NMEA parser. GGA
sentences provide position, altitude, fix quality, satellite count, and HDOP.
RMC sentences provide validity and position; velocity remains zero for now.
Parser output uses `timestamp_ms = 0` until an adapter stamps it with
system/replay time.

`nav_system_t` is public for static allocation, but the portable core owns its
contents. Normal application code should inject events and read snapshots rather
than mutating local GNSS, altitude, peer table, mode, or snapshot fields.

When local GNSS use is disabled for trilateration, the latest local GNSS sample
is still retained as local GNSS evidence for logs, snapshots, and field
comparison. This evidence must not become the accepted solution or advertised
GNSS-valid anchor telemetry while GNSS use is disabled.

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

`nav_peer_state_t` stores one peer's latest telemetry, local-to-peer range,
diagnostics, quality, last residual, and last reject reason. Telemetry and range
freshness are checked against `telemetry_ttl_ms` and `range_ttl_ms`.

For solver input, a valid range also locks `range_position` and
`range_position_timestamp_ms`: the peer coordinate that was current when that
range was accepted. Anchor selection uses this range-paired coordinate instead
of the latest peer telemetry so a later beacon cannot be combined with an older
distance measurement.

Beacon packet sequencing and ranging request correlation are separate:

- `packet_seq`: sequence number carried by peer beacon/telemetry packets.
- `last_range_request_id`: request/result correlation id for ranging attempts.

`nav_peer_beacon_rx_t` wraps `nav_peer_telemetry_t` plus local receive metadata:

- `telemetry`: what the remote peer reported.
- `rssi_dbm`: local RSSI for this received beacon.
- `snr_db`: local SNR in whole dB for v0.

RSSI/SNR are receive diagnostics, not peer-reported telemetry.

Peer telemetry position fields are also the map display position for that peer.
The peer's position source distinguishes local GNSS positions from accepted
`RADIO_3D` estimates. Only GNSS-valid peer positions may become navigation
anchors; estimated peer positions are displayable but not anchor inputs.
For map rendering, peer telemetry carries source/status metadata, and control
JSON exposes UI-facing `position_source` and `position_valid` fields alongside
the reported position, GNSS validity, and freshness/timestamp. The control app
maps `position_source = GNSS` to the GNSS marker style and
`position_source = RADIO_3D` to the no-GPS estimated marker style. `gnss_valid`
remains an anchor-eligibility signal, not the only display-position signal.
Valid control JSON `position_source` values are `GNSS`, `RADIO_3D`, and `NONE`.
`position_valid` means the control app may render the node as a live map marker;
it is separate from `gnss_valid`. Accepted degraded radio solutions keep
`position_valid = true` and expose `position_degraded` separately for styling.

The control app labels GNSS positions with the existing default node-name style,
such as `node-3`, and estimated radio positions as `node-1 (no GPS)`. The
connected node adds `this`, for example `node-3 (this)` or
`node-1 (this, no GPS)`.
GNSS markers use a blue/green success color, estimated radio markers use
amber/orange, and stale or failed states use faded or red styling. An accepted
`RADIO_3D` position is not an error state.
The map shows one marker per node from the node's current accepted solution
source. Estimated positions are shown only when that node has no usable local
GPS solution and has enough valid anchors/ranges to accept a radio solution.
Accepted degraded radio solutions remain displayable with degraded styling;
rejected or no-solution positions are not live map markers.
The four-node field visualization scenario assumes the modules are at the same
height and keeps the existing control-app/manual altitude default of `0 mm`.
The v1 radio solver remains unchanged and still receives a local altitude input;
the field workflow simply does not treat height as a visualization concern.
Map labels and overlays hide altitude for this scenario. Tables, debug details,
serial JSON, logs, and replay outputs continue to carry altitude.
The control-app map shows node markers plus local-to-peer range links labeled
with distance and age. Peer-to-peer ranges remain in tables and NDJSON captures;
anchor triangles and geometry overlays remain out of scope.
The map initially fits or follows the visible node markers. User pan or zoom
pauses automatic camera movement until the operator presses a recenter control.
In the control app layout, the map is the primary live field surface above the
existing distance and peer tables. The local-node controls remain available next
to it on wide screens and above it on narrow screens.
The map's `this`/`me` marker is the connected navigation node from the serial
snapshot, not the browser or laptop geolocation.
Recently stale node positions remain visible briefly as faded markers with age
available in details, then leave the live map while remaining diagnosable in
tables/logs. For the four-node field workflow, the app uses fresh/stale/expired
field evidence states: fresh under 5 seconds, stale from 5 to 30 seconds, and
expired after 30 seconds.
The control app keeps one USB-serial connection; other nodes appear through the
connected node's radio-derived network view, not through multiple serial ports.
Control-app map behavior should be testable from a deterministic four-node demo
snapshot or fixture: three GNSS nodes around one accepted `RADIO_3D` node.
This visualization branch prepares the control-app and serial/control contract
for real GNSS data but does not implement the ESP32 GNSS UART adapter.
Peer map labels use node IDs and the control app's local name cache/defaults;
over-the-air node-name synchronization is out of scope.
The control app can ingest the typed control-channel NDJSON envelopes directly:
`snapshot` records update the main node/peer view, `range` records update
distance tables and local range links, `node_quality` records fill debug/checklist
state and missing map positions when they carry a valid mappable position, and
plain firmware text logs are retained as `log` records.

## Pair Range Observations

`nav_peer_state_t` is the current local-to-peer anchor state. It is intentionally
not enough for a whole-network range view, because a node can overhear or receive
a range result for a pair where it is neither endpoint.

The ESP32 TDMA/ranging integration should add a separate pair-range model for
range observations identified by:

- `from_id`: scheduled ranging initiator / SX1280 ranging master.
- `to_id`: scheduled ranging peer / SX1280 ranging slave.
- `request_id`: ranging attempt correlation id.
- `range_mm`, `range_sigma_mm`, validity, freshness, RSSI/SNR diagnostics, and
  `range_fail_reason` for failed attempts.

If one endpoint is the local node, the other endpoint may update the existing
per-peer anchor range used by the solver. If neither endpoint is local, the
observation is third-party network-health data for logs, replay, and
`telemetry-ui/`; it must not be treated as a local anchor distance unless the
solver is explicitly extended to consume inter-peer constraints.

## Anchor Selection

`nav_anchor_t` is the compact solve input:

- `node_id`
- `lat_e7`, `lon_e7`, `alt_mm`
- `range_mm`, `range_sigma_mm`
- `quality`
- `reject_reason`

The position fields come from `nav_peer_state_t.range_position`, not necessarily
the latest displayed peer telemetry.

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
- local GNSS evidence for comparison, including fix health, position, age, and
  whether it was used by the accepted solution
- `total_quality`

## Config Thresholds

`nav_config_t` includes:

- `telemetry_ttl_ms`, `range_ttl_ms`, `local_altitude_ttl_ms`
- `radio_solve_interval_ms`
- `max_range_sigma_mm`
- `min_anchor_quality`, `min_solution_quality`
- `max_residual_rms_m`, `max_residual_m`
- `min_anchor_triangle_area_m2`
- `degraded_anchor_triangle_area_m2`
- `demo_force_gps_denied`
- `allow_gnss_altitude_in_demo_forced_denied`

The geometry score is a v1 horizontal triangle-area heuristic, not full GDOP.

`radio_solve_interval_ms` limits the expensive radio trilateration recompute
cadence. Events update core-owned state immediately, but a GPS-disabled node
recomputes `RADIO_3D` only from the tick path when the cadence allows it and the
selected solve inputs changed.

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
