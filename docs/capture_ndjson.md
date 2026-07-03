# Capture NDJSON Schema

A **capture session** is a newline-delimited JSON (NDJSON) recording of one
control-channel session, streamed by `telemetry-ui/` to a file on disk. It is the
input format for offline analysis and the `analyze-capture` skill. This document
is the contract that both the writer (`telemetry-ui/`) and readers (humans, the AI
agent) rely on.

During firmware-first bring-up, piping a node's USB-serial output directly to a
file also produces NDJSON records, but without the browser-added `meta` line or
`ts_ms` wall-clock timestamps. Readers must accept those direct serial captures
and use device `ts` for ordering.

The control channel itself is described in `docs/data_flow.md` and
`core/nav_serial_json`. The over-the-air source of the quality data is in
`docs/radio_protocol.md`.

## File layout

- One JSON object per line (`\n`-terminated). No trailing commas, no multi-line
  objects, no array wrapper.
- Telemetry UI recordings start with a `meta` record.
- Direct firmware serial captures may omit `meta`; their first line may be
  `snapshot`, `node_quality`, `range`, or `log`.
- Every non-`meta` line is one of `snapshot`, `node_quality`, `range`, `log`.
- Lines are appended in receive order. Readers must not assume records are sorted
  by device time; sort/merge on `ts_ms` if a global timeline is needed.
- Unknown `type` values and unknown fields must be ignored by readers
  (forward-compatible).

## Common envelope

Every record carries:

| Field | Type | Meaning |
| ----- | ---- | ------- |
| `type` | string | Record kind: `meta` \| `snapshot` \| `node_quality` \| `range` \| `log`. |
| `ts_ms` | number | Browser wall-clock at receive, epoch milliseconds. Present in telemetry-UI captures; absent in direct firmware serial captures. |
| `ts` | number | Device monotonic milliseconds (`t` from the node), if present on the source record. Resets on node reboot. |

`ts_ms` is added by `telemetry-ui/`; `ts` comes from the device. Use `ts_ms` for
cross-node and cross-reboot alignment, `ts` for intra-session device ordering.

## Units

Follows the repository convention: `_e7` = degrees × 1e7, `_mm` = millimetres,
`_m` = metres, `_ms` = milliseconds, `_centi` = value × 100, `_dbm`/`_db` =
decibels. Quality scores are `0.0..1.0` unless a `_u8` suffix indicates a
0..255-scaled wire value.

## Record types

### `meta` (first line)

Session header written once by `telemetry-ui/` when recording starts. Direct
firmware serial captures omit this record.

| Field | Type | Meaning |
| ----- | ---- | ------- |
| `type` | string | `"meta"`. |
| `ts_ms` | number | Recording start, epoch ms. |
| `schema_version` | number | This schema's version (start at `1`). |
| `app_version` | string | Telemetry UI build identifier. |
| `firmware_build` | string | Connected node's firmware build string, if known. |
| `node_id` | number | Connected node id. |
| `node_name` | string | Connected node name (may be `""`). |
| `debug` | bool | Whether debug telemetry mode was on at record start. |

### `snapshot`

The periodic (~500 ms) node snapshot, unchanged from the current control-channel
snapshot object (see `nav_serial_write_snapshot`). Carried verbatim under `data`.

| Field | Type | Meaning |
| ----- | ---- | ------- |
| `type` | string | `"snapshot"`. |
| `ts_ms` / `ts` | number | Envelope timestamps. |
| `data` | object | The snapshot object: `{t, node:{id,name,gps,mock}, mode, sol, src, reject, pos:{lat_e7,lon_e7,alt_mm}, num_anchors, peers:[{id,gnss,lat_e7,lon_e7,alt_mm,range_mm,range_valid,rssi,snr,quality}]}`. |

### `node_quality`

One decoded **node quality report** — either the connected node's own (`origin`
= `local`) or a peer's, received over the air while debug telemetry mode is
active. Mirrors the node-quality-report payload in `docs/radio_protocol.md`.

| Field | Type | Unit | Meaning |
| ----- | ---- | ---- | ------- |
| `type` | string | | `"node_quality"`. |
| `ts_ms` / `ts` | number | ms | Envelope timestamps. |
| `node_id` | number | | The node this report is about. |
| `origin` | string | | `"local"` or `"peer"`. |
| `age_ms` | number | ms | Age of the underlying report at emission (freshness). |
| `data.nav_mode` | string | | Node's nav mode (enum name). |
| `data.solution_status` | string | | Solution status (enum name). |
| `data.solution_source` | string | | `LOCAL_GNSS` / `RADIO_3D` / … . |
| `data.residual_rms_m` | number | m | Trilateration RMS residual. |
| `data.max_residual_m` | number | m | Largest per-anchor residual. |
| `data.geometry_score` | number | 0..1 | Anchor-geometry score. |
| `data.total_quality` | number | 0..1 | Combined solution quality. |
| `data.num_anchors` | number | | Anchors used. |
| `data.anchor_ids` | number[] | | Selected anchor node ids (up to 3). |
| `data.fix_type` | string | | GNSS fix type (`NONE`/`2D`/`3D`/`RTK_*`). |
| `data.satellites` | number | | Satellite count. |
| `data.hdop_centi` | number | ×100 | Horizontal DOP × 100. |
| `data.hacc_mm` | number | mm | Horizontal accuracy estimate. |
| `data.vacc_mm` | number | mm | Vertical accuracy estimate. |
| `data.lat_e7` / `data.lon_e7` | number | deg×1e7 | Reported position. |
| `data.alt_mm` | number | mm | Reported altitude. |
| `data.packet_seq` | number | | Report/beacon sequence, for loss estimation. |

### `range`

One range observation — a local ranging result/failure or an overheard
third-party pair report (`source=air_report`). Endpoint-bearing.

| Field | Type | Unit | Meaning |
| ----- | ---- | ---- | ------- |
| `type` | string | | `"range"`. |
| `ts_ms` / `ts` | number | ms | Envelope timestamps. |
| `from_id` | number | | Ranging initiator (master). |
| `to_id` | number | | Ranging peer (slave). |
| `request_id` | number | | Attempt correlation id. |
| `ok` | bool | | Whether a valid range was produced. |
| `range_mm` | number | mm | Measured range (when `ok`). |
| `range_sigma_mm` | number | mm | Range std-dev estimate. |
| `rssi_dbm` | number | dBm | Link RSSI (diagnostic only). |
| `snr_db` | number | dB | Link SNR (diagnostic only). |
| `range_fail_reason` | string | | Failure cause (enum name) when `!ok`. |
| `source` | string | | `"log"` (local) or `"air_report"` (overheard). |

### `log`

A firmware text log line, retained for context and OTA-path debugging.

| Field | Type | Meaning |
| ----- | ---- | ------- |
| `type` | string | `"log"`. |
| `ts_ms` / `ts` | number | Envelope timestamps. |
| `level` | string | Log level (`TRACE`…`ERROR`), if parsed. |
| `tag` | string | Category/tag (`RADIO`, `RANGE`, …), if parsed. |
| `text` | string | The raw log line. |

## Example (abridged)

```json
{"type":"meta","ts_ms":1751539200000,"schema_version":1,"app_version":"react-0.1.0","firmware_build":"Jul 03 2026","node_id":2,"node_name":"alpha","debug":true}
{"type":"snapshot","ts_ms":1751539200500,"ts":43945,"data":{"t":43945,"node":{"id":2,"name":"alpha","gps":false,"mock":false},"mode":"NO_NAV_SOLUTION","sol":"NONE","src":"NONE","reject":"NOT_ENOUGH_ANCHORS","pos":{"lat_e7":0,"lon_e7":0,"alt_mm":0},"num_anchors":0,"peers":[{"id":3,"gnss":false,"lat_e7":0,"lon_e7":0,"alt_mm":0,"range_mm":3420,"range_valid":true,"rssi":-48,"snr":8,"quality":0.0}]}}
{"type":"node_quality","ts_ms":1751539201100,"ts":44510,"node_id":3,"origin":"peer","age_ms":220,"data":{"nav_mode":"RADIO_NAV_OK","solution_status":"RADIO_3D","solution_source":"RADIO_3D","residual_rms_m":0.31,"max_residual_m":0.52,"geometry_score":0.87,"total_quality":0.79,"num_anchors":3,"anchor_ids":[0,1,2],"fix_type":"NONE","satellites":0,"hdop_centi":0,"hacc_mm":0,"vacc_mm":0,"lat_e7":504529000,"lon_e7":305268000,"alt_mm":183500,"packet_seq":104}}
{"type":"range","ts_ms":1751539201400,"ts":44803,"from_id":1,"to_id":2,"request_id":13,"ok":false,"range_fail_reason":"TIMEOUT","source":"air_report","rssi_dbm":-54,"snr_db":13}
{"type":"log","ts_ms":1751539201450,"ts":44810,"level":"WARN","tag":"RANGE","text":"t=44810ms [WARN] [RANGE] range_result ok=false from=1 to=2 request_id=13 range_fail_reason=TIMEOUT"}
```

## Notes for readers / the analyzer

- Treat `node_quality` and `range` (`source=air_report`) as **diagnostics with
  freshness**; they are never anchor inputs to a solver.
- Apparent packet loss for a node can be estimated from gaps in `packet_seq`
  across its `node_quality`/beacon records.
- A capture may contain **no** `RADIO_3D` solutions (distance-only runs); in that
  case the meaningful signal is network/link quality from `range` records, not
  trilateration quality.
