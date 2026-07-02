# Logging

The portable core never prints directly. It emits structured text through
`nav_logger_t`; ports decide where the records go.

The repository-root ESP-IDF diagnostic firmware logs directly to the configured
ESP-IDF console with one complete line per write:

```text
t=1234ms [INFO] [SYSTEM] Health summary: RADIO=OK GPS=DISABLED COMPASS=DISABLED | ...
```

The `t=` value is milliseconds elapsed since `Logger::begin()`.

## Categories And Levels

Categories: `BOOT`, `CONFIG`, `GNSS`, `RADIO_PROTO`, `PEER_TABLE`, `RANGE`,
`QUALITY`, `STATE`, `SOLUTION`, `REPLAY`, `SIM`, `ERROR`.

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
- `QUALITY anchor_accepted`
- `QUALITY anchor_rejected`
- `SOLUTION solve_attempt`
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
t=1000 level=INFO cat=SOLUTION event=solve_succeeded lat_e7=504529000 lon_e7=305268000 alt_mm=183500 residual_rms_m=0.000162 max_residual_m=0.000197 quality=1.000 geometry_score=1.000
```

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
firmware logs hardware attempts with explicit `from` and `to` endpoints:

```text
t=43945ms [WARN] [RANGE] range_result ok=false from=1 to=2 request_id=13 range_fail_reason=RANGING_ENGINE_ERROR raw_reg=-69 uncorrected_m=-1.55 elapsed_ms=14 irq=0x0200 flags="master_result_valid" note="invalid distance"
t=45403ms [INFO] [RANGE] range_result ok=false from=2 to=0 request_id=5 range_fail_reason=TIMEOUT elapsed_ms=359 error=-901 note="ranging timeout" source=air_report heard_by=1 report_rssi_dbm=-53.0 report_snr_db=13.5
```

`source=air_report` means the connected node heard another node's compact
best-effort range report over SX1280 packet RX. Those third-party pair ranges
are network-health/control-app observations only and must not be treated as
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
