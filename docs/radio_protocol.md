# Radio Protocol (on-device, over-the-air)

## Purpose

This protocol is the **over-the-air packet format exchanged between nodes** on
the single-hop TDMA network. Every node drives its SX1280 directly and runs the
navigation core locally; there is no separate radio MCU in the current design
(see ADR 0002, superseded).

A node uses this protocol to:

- broadcast its own telemetry beacon (position, velocity, GNSS validity, nav
  mode) in its TDMA slot, and
- run SX1280 ranging-engine exchanges with peers during the ranging cycle and
  publish the resulting distance as range events.

Each receiving node feeds incoming packets into its own `core/` as `nav_event_t`,
which builds the per-peer "network view" (system view) and runs trilateration.

> The **control channel** to the host control-app is a different link: newline
> delimited JSON over the USB-serial port. It is not this radio protocol. See
> `docs/data_flow.md` / `nav_serial_json`.

## Non-Goals

This protocol does not define SX1280 SPI register operations, ELRS behavior, RF
calibration, antenna tuning, flight-controller protocol, multi-hop relaying,
software time-of-arrival ranging, RSSI-based distance estimation, or
encryption/authentication for v1.

## Naming Rules

- `packet_seq`: radio beacon packet sequence from a peer node.
- `request_id`: command/result correlation ID for ranging attempts.
- `from_id`: scheduled ranging initiator; for SX1280 ranging this is the
  ranging master that reads the engine result.
- `to_id`: scheduled ranging peer; for SX1280 ranging this is the ranging
  slave for that exchange.
- `peer_id`: acceptable for telemetry sender identity and current local-to-peer
  host/replay range structs, but ambiguous for third-party pair ranges.
- `range_fail_reason`: radio-layer failure cause.
- `reject_reason`: navigation-layer anchor/solution rejection cause.

Do not collapse these into one generic `sequence` field in public protocol or
navigation types.

## Byte Order And Units

- Integer fields are little-endian; signed fields are two's-complement.
- Timestamps are milliseconds.
- Latitude/longitude use signed e7 degrees: `lat_e7_i32`, `lon_e7_i32`.
- Altitude uses millimeters: `alt_mm_i32`.
- Velocity uses millimeters per second: `vel_n_mmps_i32`, etc.
- Navigation-core ranges are never negative and use `uint32_t range_mm`.
  Protocol range fields use `range_mm_u32`; a decoder must reject negative wire
  range data before producing `nav_range_result_t`.
- Range sigma uses millimeters: `range_sigma_mm_u32`.
- RSSI uses dBm: `rssi_dbm_i16`. SNR uses whole dB for v1: `snr_db_i16`.

## Frame Format

Current frame in `nav_radio_protocol.c` (used by host tests and current
placeholder packet code):

```text
magic0_u8 magic1_u8 message_type_u8 packet_seq_u16 payload_length_u8 payload[N]
```

Air hardening (future): wrap the payload in COBS with a trailing CRC32 so partial
or corrupted SX1280 receptions are dropped cleanly:

```text
0x00 COBS( magic_u16 protocol_version_u8 message_type_u8 packet_seq_u16
           flags_u16 payload_length_u16 payload_bytes[N] crc32_u32 ) 0x00
```

| Feature | Status |
| ------- | ------ |
| message enums (`nav_radio_message_type_t`) | implemented |
| payload structs | implemented |
| placeholder encode/decode | implemented (host tests + air v1) |
| COBS + CRC32 air hardening | future |

## Distance Measurement Source

Node-to-node distance comes from the SX1280 ranging engine. TDMA only chooses
when a pair may use the radio; it does not estimate distance from packet
timestamps, host-side round-trip time, RSSI, or SNR.

During telemetry slots the radio uses normal packet TX/RX and carries
`BEACON_RX` frames. During ranging slots the port switches the SX1280 into
ranging mode. The scheduled initiator acts as SX1280 ranging master, the peer
acts as SX1280 ranging slave, and the master's readable ranging result becomes a
`NAV_EVT_RANGE_RESULT`. The ESP32-S3 bring-up flow in
`examples/esp32s3-ranging` is the reference hardware workflow for this slot.

RSSI and SNR attached to range or beacon events are link diagnostics only. They
must not be used as replacement distance measurements.

## Pair Range Visibility

Hardware TDMA ranging must identify both endpoints of every range observation.
The planned air/control contract uses `from_id` and `to_id` so any listener can
understand that a measurement is for pair A-B even when the listener is node C.

The current C host-test payloads still carry a single `peer_id`, where the local
node is implicit. That is sufficient for existing local-to-peer replay fixtures,
but it is not sufficient for ESP32 TDMA hardware integration or a whole-network
range view in `control-app/`.

Until solver behavior is explicitly extended, only ranges where one endpoint is
the local node are eligible anchor ranges. Third-party pair ranges must be kept
as network-health/control-app observations, not folded into the per-peer anchor
table as local distances.

## Message Types

| Message | Sender → Receiver | Purpose | Core event produced |
| ------- | ----------------- | ------- | ------------------- |
| `BEACON_RX` (telemetry beacon) | peer → all | Peer telemetry + local RX metadata. | `NAV_EVT_PEER_TELEMETRY_RX` |
| `REQUEST_RANGE` | node → peer | Schedule/correlate one SX1280 ranging-engine attempt. | none directly |
| `RANGE_RESULT` | ranging master → all | Successful SX1280 ranging-engine result for `from_id`/`to_id`. | `NAV_EVT_RANGE_RESULT` for local endpoint ranges; pair-range observation for third-party ranges |
| `RANGE_FAIL` | ranging master → all | Failed SX1280 ranging-engine attempt for `from_id`/`to_id`. | `NAV_EVT_RANGE_FAIL` for local endpoint failures; pair-range observation for third-party failures |
| `HEARTBEAT` | peer → all | Liveness / status flags. | liveness only |
| `STATUS` / `STATS` | peer → all | Detailed state / link counters. | diagnostics only |
| `LOG_TEXT` | peer → all | Optional diagnostic text. | diagnostic log only |

`SET_NODE_ID` / `SET_CONFIG` are **local configuration** applied on the node
(now via the control-app over USB-serial), not air messages.

## Payload Schemas

The public header mirrors the implemented schemas as typed payload structs. Do
not serialize raw C structs directly unless packing and alignment are
controlled. Endpoint-bearing range payloads below are the required replacement
contract before real ESP32 TDMA ranging is treated as the air protocol.

### `BEACON_RX` (telemetry beacon)

Telemetry fields are what the remote peer claims. `rssi_dbm_i16` and `snr_db_i16`
are local diagnostics measured by the receiving radio.

```text
peer_id_u8
packet_seq_u32
radio_timestamp_ms_u32
lat_e7_i32
lon_e7_i32
alt_mm_i32
vel_n_mmps_i32
vel_e_mmps_i32
vel_d_mmps_i32
gnss_fix_type_u8
gnss_valid_u8
nav_mode_u8
rssi_dbm_i16
snr_db_i16
reserved_u8
```

Maps to `nav_peer_beacon_rx_t` → `NAV_EVT_PEER_TELEMETRY_RX`.

### `RANGE_RESULT`

Implemented host/replay payload with implicit local endpoint:

```text
peer_id_u8
range_status_u8
radio_timestamp_ms_u32
range_mm_u32
range_sigma_mm_u32
rssi_dbm_i16
snr_db_i16
attempt_count_u8
request_id_u16
```

Maps to `nav_range_result_t`. `range_mm` must be positive before the navigation
core can use it as an anchor. `range_mm` is the converted SX1280 ranging-engine
distance, after any explicit calibration/correction chosen by the port.

Required endpoint-bearing hardware TDMA payload:

```text
from_id_u8
to_id_u8
range_status_u8
radio_timestamp_ms_u32
range_mm_u32
range_sigma_mm_u32
rssi_dbm_i16
snr_db_i16
attempt_count_u8
request_id_u16
```

`from_id` is the scheduled initiator / SX1280 ranging master. `to_id` is the
scheduled peer / SX1280 ranging slave. If the receiver's local node id is
neither endpoint, the decoded result updates the pair-range/network-health view
only. If one endpoint is local, the other endpoint is the anchor peer for the
existing navigation solve path.

### `RANGE_FAIL`

Implemented host/replay payload with implicit local endpoint:

```text
peer_id_u8
range_fail_reason_u8
radio_timestamp_ms_u32
attempt_count_u8
last_rssi_dbm_i16
last_snr_db_i16
request_id_u16
```

Maps to `nav_range_failure_t` with `nav_range_fail_reason_t`. Radio fail reason
and navigation reject reason stay separate:

- `range_fail_reason`: why the radio/ranging attempt failed.
- `reject_reason`: why the navigation core rejected an anchor or solution.

Required endpoint-bearing hardware TDMA payload:

```text
from_id_u8
to_id_u8
range_fail_reason_u8
radio_timestamp_ms_u32
attempt_count_u8
last_rssi_dbm_i16
last_snr_db_i16
request_id_u16
```

Third-party failures should appear in the same pair-range/network-health view as
third-party successful ranges so the control app can show stale, failed, or
missing links between non-local nodes.

### `HEARTBEAT`

```text
node_id_u8
uptime_ms_u32
status_flags_u32
```

### `REQUEST_RANGE`

Implemented host/replay payload with implicit local endpoint:

```text
peer_id_u8
request_id_u16
timeout_ms_u16
```

`request_id` must be copied into the matching `RANGE_RESULT` or `RANGE_FAIL`.

Required endpoint-bearing hardware TDMA payload:

```text
from_id_u8
to_id_u8
request_id_u16
timeout_ms_u16
```

For the scheduled slot, `from_id` must match the ranging master and `to_id` must
match the ranging slave.

## Range Failure Reasons

`nav_range_fail_reason_t`:

- `NAV_RANGE_FAIL_NONE`
- `NAV_RANGE_FAIL_TIMEOUT`
- `NAV_RANGE_FAIL_NO_RESPONSE`
- `NAV_RANGE_FAIL_RADIO_BUSY`
- `NAV_RANGE_FAIL_BAD_FRAME`
- `NAV_RANGE_FAIL_RANGING_ENGINE_ERROR`
- `NAV_RANGE_FAIL_ABORTED`
- `NAV_RANGE_FAIL_UNKNOWN`

These are radio-layer causes and must not be stored as `nav_reject_reason_t`.

## TDMA Timing (single-hop)

- One node is the configured **TDMA time authority**; its heartbeat defines the
  frame timing (see CONTEXT.md).
- A frame interleaves a **telemetry cycle** (each node beacons in normal packet
  mode) and a **ranging cycle** (a scheduled pass over unique peer pairs using
  the SX1280 ranging engine).
- Streaming events (`BEACON_RX`, `RANGE_RESULT`) are not ACKed in v1.

## Mapping To `nav_event_t`

- `BEACON_RX` → `NAV_EVT_PEER_TELEMETRY_RX` carrying `nav_peer_beacon_rx_t`
- `RANGE_RESULT` → `NAV_EVT_RANGE_RESULT` when one endpoint is local; otherwise
  a pair-range/network-health observation.
- `RANGE_FAIL` → `NAV_EVT_RANGE_FAIL` when one endpoint is local; otherwise a
  pair-range/network-health observation.
- `STATUS` / `HEARTBEAT` / `STATS` / `LOG_TEXT` → diagnostics / liveness only

Local GNSS and local altitude samples are produced by the node's own sensors
(or the mock injector / replay), not by the radio.

## Example Decoded Messages

Implemented `RANGE_RESULT` from peer 2 with implicit local endpoint:

```text
message_type=RANGE_RESULT packet_seq=42
peer_id_u8=2 request_id_u16=77 range_status_u8=0
radio_timestamp_ms_u32=123456 range_mm_u32=621957 range_sigma_mm_u32=100
rssi_dbm_i16=-61 snr_db_i16=10 attempt_count_u8=1
```

Host mapping:

```text
NAV_EVT_RANGE_RESULT peer_id=2 request_id=77 range_mm=621957 range_sigma_mm=100 rssi_dbm=-61 snr_db=10
```

Planned endpoint-bearing `RANGE_RESULT` for pair 1-2, heard by node 3:

```text
message_type=RANGE_RESULT packet_seq=42
from_id_u8=1 to_id_u8=2 request_id_u16=77 range_status_u8=0
radio_timestamp_ms_u32=123456 range_mm_u32=621957 range_sigma_mm_u32=100
rssi_dbm_i16=-61 snr_db_i16=10 attempt_count_u8=1
```

Node 3 maps this to a pair-range/network-health observation, not to its local
anchor table, because neither endpoint is node 3.
