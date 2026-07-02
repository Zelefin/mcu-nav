# ESP32 SX1280 Ranging Slots

## What to build

Use the SX1280 hardware ranging engine during scheduled ranging slots. The
scheduled ranging master reads the SX1280 result and injects endpoint-bearing
range success or failure observations. The scheduled ranging slave responds
through the SX1280 ranging engine and does not invent distance data.

## Acceptance criteria

- [ ] Scheduled ranging slots choose a `from_id` ranging master and `to_id`
      ranging slave.
- [ ] ESP32 ranging master behavior follows the validated SX1280 ranging
      workflow already proven by the repository bring-up example.
- [ ] Successful ranging engine results become endpoint-bearing range result
      observations with `request_id`.
- [ ] Timeout, busy, bad frame, aborted, and ranging engine failures become
      endpoint-bearing range failure observations with `range_fail_reason`.
- [ ] RSSI/SNR are logged or stored as diagnostics only.
- [ ] Ranging does not use RSSI, packet timing, or host round-trip time as a
      distance source.
- [ ] Two-node hardware tests show successful and failed attempts clearly in
      logs or control-app state.

## Blocked by

- Working PoC Hardware Integration Gate
- Endpoint-Bearing Range Contract
- ESP32 TDMA Telemetry Path
