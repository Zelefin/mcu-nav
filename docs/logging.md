# Logging

The portable core never prints directly. It emits structured text through
`nav_logger_t`; ports decide where the records go.

The repository-root ESP-IDF diagnostic firmware emits logs on the USB-serial
control channel as typed NDJSON `log` records, one JSON object per line:

```json
{"type":"log","ts":1234,"level":"INFO","tag":"SYSTEM","text":"t=1234ms [INFO] [SYSTEM] Health summary: RADIO=OK GPS=OK | radio tx=1 rx=1 gps_bytes=340 gps_sentences=5 gps_fix=3"}
```

The `ts` envelope value and the `t=` value embedded in `text` are milliseconds
elapsed since `Logger::begin()`.

## Categories And Levels

Categories: `BOOT`, `CONFIG`, `GNSS`, `RADIO_PROTO`, `PEER_TABLE`, `RANGE`,
`TDMA`, `QUALITY`, `STATE`, `SOLUTION`, `REPLAY`, `SIM`, `ERROR`.

Levels: `TRACE`, `DEBUG`, `INFO`, `WARN`, `ERROR`.

## Text Format

Ports should format callback data like:

```text
t=123456 level=INFO cat=PEER_TABLE event=telemetry_update peer=2 packet_seq=104 age_ms=0 lat_e7=... lon_e7=... alt_mm=...
```

## Implemented Events

- `BOOT core_init`
- `GNSS local_gnss_sample`
- `GNSS local_altitude_accepted`
- `GNSS local_altitude_rejected`
- `PEER_TABLE telemetry_update`
- `PEER_TABLE peer_marked_stale`
- `RANGE range_update`
- `RANGE range_fail`
- `TDMA authority_sync`
- `TDMA authority_missing`
- `TDMA authority_mismatch`
- `TDMA frame_start`
- `TDMA slot_start`
- `TDMA slot_decision`
- `TDMA slot_missed`
- `TDMA slot_complete`
- `QUALITY anchor_accepted`
- `QUALITY anchor_rejected`
- `SOLUTION solve_attempt`
- `SOLUTION solve_skipped`
- `SOLUTION solve_succeeded`
- `SOLUTION solve_rejected`
- `SOLUTION residual`
- `STATE mode_transition`
- `SOLUTION solution_status_transition`
- `REPLAY replay_start`
- `REPLAY event_applied`
- `REPLAY truth_compare`
- `REPLAY replay_summary`

## Examples

Accepted anchor:

```text
t=1000 level=DEBUG cat=QUALITY event=anchor_accepted peer=2 quality=1.000 range_mm=621957 range_sigma_mm=100
```

Rejected anchor:

```text
t=2001 level=WARN cat=QUALITY event=anchor_rejected peer=3 reason=STALE_RANGE
```

Solve success:

```text
t=1000 level=INFO cat=SOLUTION event=solve_succeeded lat_e7=504529000 lon_e7=305268000 alt_mm=183500 residual_rms_m=0.000162 max_residual_m=0.000197 quality=1.000 geometry_score=1.000 iterations=3
```

Skipped solve:

```text
t=1200 level=DEBUG cat=SOLUTION event=solve_skipped reason=CADENCE elapsed_ms=200 interval_ms=500
```

The snapshot also carries structured `radio_solve` diagnostics so captures can
distinguish `SOLVED`, `REJECTED`, `SKIPPED_CADENCE`, and
`SKIPPED_UNCHANGED_INPUTS` without relying on debug-level text logs.

TDMA field reconstruction logs are always on in the first TDMA field
implementation. They are local/control-channel logs, not OTA debug telemetry;
the ADR 0004 debug telemetry flag still controls extra over-the-air node-quality
traffic.

TDMA slot logs include enough shared fields to reconstruct the schedule from a
single captured node:

```text
node_id=<id>
local_ms=<ms>
frame_index=<n>
slot_index=<n>
slot_ms=<ms>
remaining_ms=<ms>
action=<beacon|range|listen>
role=<authority|follower|ranging_master|ranging_slave|listener>
from_id=<id|255>
to_id=<id|255>
peer_id=<id|255>
authority_age_ms=<ms>
sync_state=<authority|synced|waiting|expired>
reason=<text>
```

Detailed SX1280 exchange fields remain on `RANGE` logs, including
`request_id`, `elapsed_ms`, `irq`, `flags`, `error`, `rssi_dbm`, `snr_db`, and
`range_mm`.

Solve failure:

```text
t=1000 level=WARN cat=SOLUTION event=solve_rejected reason=MISSING_LOCAL_ALTITUDE
```

Residual diagnostic:

```text
t=1000 level=DEBUG cat=SOLUTION event=residual peer=1 residual_mm=0 expected_range_m=394.134932
```

Mode transition:

```text
t=1000 level=INFO cat=STATE event=mode_transition from=NO_NAV_SOLUTION to=RADIO_NAV_OK
```

Range update:

```text
t=1000 level=INFO cat=RANGE event=range_update peer=2 request_id=77 range_mm=621957 range_sigma_mm=100 valid=1 rssi_dbm=-61 snr_db=10
```

Current replay/core range events are local-to-peer, but the ESP32 distance-only
firmware emits typed `range` records with explicit `from_id` and `to_id`
endpoints. It also keeps the human-readable message inside a typed `log` record:

```json
{"type":"range","ts":43945,"from_id":1,"to_id":2,"request_id":13,"ok":false,"range_fail_reason":"RANGING_ENGINE_ERROR","rssi_dbm":0,"snr_db":0,"source":"log"}
{"type":"log","ts":45403,"level":"INFO","tag":"RANGE","text":"t=45403ms [INFO] [RANGE] range_result ok=false from=2 to=0 request_id=5 range_fail_reason=TIMEOUT elapsed_ms=359 error=-901 note=\"ranging timeout\" source=air_report heard_by=1 report_rssi_dbm=-53.0 report_snr_db=13.5"}
{"type":"range","ts":45403,"from_id":2,"to_id":0,"request_id":5,"ok":false,"range_fail_reason":"TIMEOUT","rssi_dbm":-53,"snr_db":14,"source":"air_report"}
```

`source=air_report` means the connected node heard another node's compact
best-effort range report over SX1280 packet RX. Those third-party pair ranges
are network-health/telemetry-UI observations only and must not be treated as
anchor updates for the local solver.

## Replay Outputs

`nav_replay` writes deterministic CSV/log outputs from host replay fixtures:

- `events.csv`: raw event input for replay.
- `peers.csv`: one row per present peer after every processed input row.
- `solution.csv`: one snapshot row after every processed input row.
- `logs.txt`: replay lifecycle logs plus structured core logs.
- `truth.csv`: optional replay input for exact-time comparison.
- `compare_report.txt` / `compare_report.json`: emitted when truth is present.

`truth.csv` is not produced by replay. `tools/sim/generate_scenario.py` may
generate it as comparison input. See `docs/replay_csv.md` for exact replay
schemas.
