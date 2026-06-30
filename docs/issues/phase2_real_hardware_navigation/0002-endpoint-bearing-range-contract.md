# Endpoint-Bearing Range Contract

## What to build

Move range result and range failure behavior from implicit local `peer_id`
semantics to explicit endpoint semantics using `from_id` and `to_id`. The
contract must be visible through telemetry codec behavior, replay, logs, and
tests before hardware TDMA ranging relies on it.

## Acceptance criteria

- [ ] Successful range observations carry `from_id`, `to_id`, `request_id`,
      range value, range sigma, validity, RSSI, SNR, and attempt diagnostics.
- [ ] Failed range observations carry `from_id`, `to_id`, `request_id`,
      `range_fail_reason`, attempt diagnostics, and last RSSI/SNR diagnostics.
- [ ] `packet_seq`, `request_id`, `range_fail_reason`, and `reject_reason`
      remain separate in public contracts and docs.
- [ ] Replay input schema and fixtures cover endpoint-bearing local-endpoint
      and third-party range observations.
- [ ] Existing local-to-peer behavior remains deterministic for the solver.
- [ ] Docs that define replay, radio protocol, data model, and data flow are
      updated in the same change.
- [ ] Host tests cover encode/decode, replay parsing, negative input handling,
      and deterministic output.

## Blocked by

- Working PoC Hardware Integration Gate
