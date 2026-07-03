# Pair Range Network View

## What to build

Add a whole-network pair range view for successful and failed range
observations. Local-endpoint observations may update the existing anchor state;
third-party observations must remain network-health data and must not become
local anchor distances.

The pair range view must be visible through snapshots or serial JSON and through
the telemetry UI so a user connected to one node can inspect A-B, A-C, and B-C
range health.

## Acceptance criteria

- [ ] Pair range observations store `from_id`, `to_id`, `request_id`,
      freshness, validity, range value when present, RSSI/SNR diagnostics, and
      `range_fail_reason` when failed.
- [ ] Local-endpoint ranges continue to feed the existing solver anchor path.
- [ ] Third-party ranges are visible for diagnostics but do not make a peer
      usable as a local anchor.
- [ ] Serial JSON exposes the pair range view in a compact, deterministic shape.
- [ ] The telemetry UI shows pair range health for local and third-party pairs.
- [ ] Replay fixtures and tests cover local-endpoint and third-party pairs.
- [ ] Docs for data model, data flow, replay, and radio protocol are
      synchronized.

## Blocked by

- Endpoint-Bearing Range Contract
