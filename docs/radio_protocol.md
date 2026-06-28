# Radio Protocol Contract Draft

## Purpose

This protocol connects:

```text
main MCU navigation core <-> radio/ranging coprocessor
```

The future radio coprocessor owns SX1280/SX1281/SX1262 driver details, ranging
procedure, beacon TX/RX, radio timing/slots, RSSI/SNR extraction, and radio
statistics.

This main MCU repository owns GNSS validity, peer table state, anchor selection,
trilateration, navigation state machine, structured logs, snapshots, and future
flight-controller output.

This repository defines the main-MCU side contract. It does not implement the
ESP8285/SX1280 firmware.

## Non-Goals

This protocol does not define SX1280 SPI register operations, ELRS behavior, RF
calibration, antenna tuning, flight-controller protocol, or
encryption/authentication for v0.

## Naming Rules

- `frame_seq`: transport frame sequence between MCU and coprocessor.
- `packet_seq`: radio beacon packet sequence from a peer node.
- `request_id`: command/result correlation ID for ranging attempts.
- `range_fail_reason`: radio-layer failure cause.
- `reject_reason`: navigation-layer anchor/solution rejection cause.

Do not use one generic `sequence` field in public protocol or navigation types.

## Transport Assumptions

For v0, assume UART serial between the main MCU and the coprocessor. Later SPI or
another transport can reuse the same message layer if it preserves frame
boundaries and byte order.

## Byte Order And Units

- Integer fields are little-endian.
- Signed fields use two's-complement representation.
- Timestamps are milliseconds.
- Latitude/longitude use signed e7 degrees: `lat_e7_i32`, `lon_e7_i32`.
- Altitude uses millimeters: `alt_mm_i32`.
- Velocity uses millimeters per second: `vel_n_mmps_i32`, etc.
- Navigation-core ranges are never negative and use `uint32_t range_mm`.
- Protocol range fields use `range_mm_u32`.
- If a future wire decoder receives signed range data, negative values must be
  rejected before producing `nav_range_result_t`.
- Range sigma uses millimeters: `range_sigma_mm_u32`.
- RSSI uses dBm: `rssi_dbm_i16`.
- SNR uses whole dB for v0: `snr_db_i16`.
- If scaled SNR is introduced later, rename the field explicitly, for example
  `snr_qdb_i16` or `snr_centi_db_i16`.

## Frame Format Draft

Proposed UART wire frame:

```text
0x00
COBS(payload)
0x00
```

Decoded payload:

```text
magic_u16
protocol_version_u8
message_type_u8
frame_seq_u16
flags_u16
payload_length_u16
payload_bytes[N]
crc32_u32
```

Implementation status: current code has placeholder encode/decode only. Full
COBS plus CRC32 framing is specified here for the future radio repo and should
be implemented later.

Current placeholder frame in `nav_radio_protocol.c`:

```text
magic0_u8 magic1_u8 message_type_u8 frame_seq_u16 payload_length_u8 payload[N]
```

It exists only for host tests and enum/type coverage.

## Message Direction Table

| Message | Direction | Ack? | Purpose | Core event produced |
| ------- | --------- | ---- | ------- | ------------------- |
| `SET_NODE_ID` | MCU -> Radio | yes | Configure radio node id. | none |
| `SET_CONFIG` | MCU -> Radio | yes | Configure radio parameters. | none |
| `SET_SLOT_CONFIG` | MCU -> Radio | yes | Configure TDMA/ranging slot plan. | none |
| `SEND_BEACON` | MCU -> Radio | optional | Ask radio to transmit current telemetry beacon. | none |
| `REQUEST_RANGE` | MCU -> Radio | yes | Start one ranging attempt to a peer. | none directly |
| `GET_STATUS` | MCU -> Radio | yes | Request radio status. | none |
| `RESET` | MCU -> Radio | yes | Reset radio coprocessor. | none |
| `HEARTBEAT` | Radio -> MCU | no | Liveness and status flags. | diagnostics only |
| `STATUS` | Radio -> MCU | no | Detailed radio state. | diagnostics only |
| `BEACON_RX` | Radio -> MCU | no | Peer telemetry plus receive metadata. | `NAV_EVT_PEER_TELEMETRY_RX` |
| `RANGE_RESULT` | Radio -> MCU | no | Successful ranging result. | `NAV_EVT_RANGE_RESULT` |
| `RANGE_FAIL` | Radio -> MCU | no | Failed ranging attempt. | `NAV_EVT_RANGE_FAIL` |
| `STATS` | Radio -> MCU | no | Link/ranging counters. | diagnostics only |
| `LOG_TEXT` | Radio -> MCU | no | Coprocessor diagnostic text. | diagnostic log only |

## Payload Schemas

The public header mirrors these schemas as typed payload structs. Do not
serialize raw C structs directly unless packing and alignment are explicitly
controlled by the transport implementation.

### `BEACON_RX`

`BEACON_RX` contains peer telemetry plus local receive metadata. The telemetry
fields are what the remote peer claims. `rssi_dbm_i16` and `snr_db_i16` are local
diagnostics measured by the receiving radio.

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

Host mapping:

```text
nav_peer_beacon_rx_t {
    telemetry.packet_seq = packet_seq_u32
    telemetry.position = lat/lon/alt fields
    rssi_dbm = rssi_dbm_i16
    snr_db = snr_db_i16
}
```

### `RANGE_RESULT`

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

Host mapping: `nav_range_result_t`. `range_mm` must be positive before the
navigation core can use it as an anchor.

### `RANGE_FAIL`

```text
peer_id_u8
range_fail_reason_u8
radio_timestamp_ms_u32
attempt_count_u8
last_rssi_dbm_i16
last_snr_db_i16
request_id_u16
```

Host mapping: `nav_range_failure_t` with `nav_range_fail_reason_t`.

Radio fail reason and navigation reject reason are intentionally separate:

- `range_fail_reason`: why the radio/ranging attempt failed.
- `reject_reason`: why the navigation core rejected an anchor or solution.

Example: `NAV_RANGE_FAIL_TIMEOUT` can make a range stale later, which may lead to
`NAV_REJECT_STALE_RANGE`, but those are different decisions at different layers.

### `STATUS`

```text
node_id_u8
radio_mode_u8
radio_fw_version_u32
uptime_ms_u32
last_error_u16
capability_flags_u32
```

Diagnostics only. It does not directly affect solve input.

### `HEARTBEAT`

```text
node_id_u8
uptime_ms_u32
status_flags_u32
```

Used for liveness and heartbeat timeout handling.

### `SET_NODE_ID`

```text
node_id_u8
```

### `REQUEST_RANGE`

```text
peer_id_u8
request_id_u16
timeout_ms_u16
```

`request_id` must be copied into the matching `RANGE_RESULT` or `RANGE_FAIL`.

## Range Failure Reasons

`nav_range_fail_reason_t` currently defines:

- `NAV_RANGE_FAIL_NONE`
- `NAV_RANGE_FAIL_TIMEOUT`
- `NAV_RANGE_FAIL_NO_RESPONSE`
- `NAV_RANGE_FAIL_RADIO_BUSY`
- `NAV_RANGE_FAIL_BAD_FRAME`
- `NAV_RANGE_FAIL_RANGING_ENGINE_ERROR`
- `NAV_RANGE_FAIL_ABORTED`
- `NAV_RANGE_FAIL_UNKNOWN`

These are radio-layer causes and must not be stored as `nav_reject_reason_t`.

## Sequence, ACK, And Timeout Rules

- `frame_seq` increments per transmitted transport frame.
- Config/control commands should be ACKed.
- Streaming events such as `BEACON_RX` and `RANGE_RESULT` do not need ACK in v0.
- Receiver can detect `frame_seq` gaps but does not need retransmission in v0.
- Suggested command timeout: 250 ms on UART.
- Suggested heartbeat interval: 1000 ms.
- Suggested heartbeat timeout: 3000 ms before declaring coprocessor stale.

ACK payload details are not implemented yet.

## Mapping To `nav_event_t`

- `BEACON_RX` -> `NAV_EVT_PEER_TELEMETRY_RX` carrying `nav_peer_beacon_rx_t`
- `RANGE_RESULT` -> `NAV_EVT_RANGE_RESULT`
- `RANGE_FAIL` -> `NAV_EVT_RANGE_FAIL`
- `STATUS` -> diagnostics only
- `HEARTBEAT` -> diagnostics/liveness only
- `LOG_TEXT` -> diagnostic log only

Local GNSS and local altitude samples are produced by main-MCU sensors, replay,
or simulation, not by the radio coprocessor.

## Cross-Repo Stability Notes

Once the ESP8285/SX1280 repository begins, changes to message IDs, payload
schemas, units, enum values, or field names must be deliberate and documented.
The radio repo and this main MCU repo should update together when contract
changes are made.

## Implementation Status

| Feature | Specified | Implemented in this repo | Notes |
| ------- | --------: | -----------------------: | ----- |
| message enums | yes | yes | `nav_radio_message_type_t` |
| payload structs | yes | yes | Header structs mirror draft fields |
| placeholder encode/decode | yes | yes | Small host-test frame, not final wire format |
| COBS framing | yes | no | Future milestone |
| CRC32 | yes | no | Future milestone |
| ACK handling | draft | no | Future milestone |
| timeout handling | draft | no | Port/radio milestone |
| serial driver | no | no | Belongs in `ports/` or radio repo |
| ESP8285 firmware | no | no | Separate future repository |

## Example Decoded Messages

`RANGE_RESULT` from peer 2:

```text
message_type=RANGE_RESULT
frame_seq=42
peer_id_u8=2
request_id_u16=77
range_status_u8=0
radio_timestamp_ms_u32=123456
range_mm_u32=621957
range_sigma_mm_u32=100
rssi_dbm_i16=-61
snr_db_i16=10
attempt_count_u8=1
```

Host mapping:

```text
NAV_EVT_RANGE_RESULT peer_id=2 request_id=77 range_mm=621957 range_sigma_mm=100 rssi_dbm=-61 snr_db=10
```

`BEACON_RX` from peer 3:

```text
message_type=BEACON_RX
frame_seq=43
peer_id_u8=3
packet_seq_u32=104
radio_timestamp_ms_u32=123500
lat_e7_i32=504510000
lon_e7_i32=305340000
alt_mm_i32=175000
vel_n_mmps_i32=0
vel_e_mmps_i32=0
vel_d_mmps_i32=0
gnss_fix_type_u8=3
gnss_valid_u8=1
nav_mode_u8=2
rssi_dbm_i16=-64
snr_db_i16=9
```

Host mapping:

```text
NAV_EVT_PEER_TELEMETRY_RX node_id=3 packet_seq=104 lat_e7=504510000 lon_e7=305340000 alt_mm=175000 rssi_dbm=-64 snr_db=9
```
