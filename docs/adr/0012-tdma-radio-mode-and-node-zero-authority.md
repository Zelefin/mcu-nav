# ADR 0012: TDMA Radio Mode And Node Zero Authority

## Status

**Accepted** (2026-07).

## Decision

Configured navigation nodes use TDMA as the normal over-the-air radio mode for
telemetry and SX1280 ranging. The first implementation uses a fixed four-node
frame for node IDs `0..3`, 500 ms slots, and node `0` as the fixed TDMA time
authority. Node `0` publishes frame timing in its heartbeat so nodes `1..3` can
align their local TDMA schedules; active membership, failover, configurable
authority selection, and adaptive slot timing are deferred until the fixed
schedule is proven in field tests.

## Rationale

Field captures showed many SX1280 ranging timeouts even when successful packets
had usable RSSI/SNR, which points at radio-time coordination rather than only RF
link budget. A fixed node `0` authority gives all nodes one shared timing source
without introducing election or failover behavior before the basic frame timing
is verified.

## Consequences

- Node `0` must be powered and configured for the TDMA network to remain
  scheduled in the first implementation.
- Node `0`'s heartbeat is timing-bearing, not just a liveness marker.
- Node `0` obeys the same slot schedule as every other node; its only special
  authority behavior is publishing valid timing in its heartbeat.
- Followers reject timing heartbeats whose schedule parameters do not match the
  fixed 500 ms, four-node frame, log the mismatch, and keep the previous valid
  timing until it expires.
- The first frame always reserves slots for node IDs `0..3`; unused or missing
  nodes produce quiet slots instead of changing the schedule.
- The first slot duration is 500 ms, producing a 5 second full frame.
- Each ranging slot performs one SX1280 ranging exchange and records success or
  failure; retries and multi-sample ranging are deferred until the fixed timing
  is reliable.
- A node that reaches its scheduled action too late skips that action, logs the
  missed slot, and returns to listening instead of overrunning the next slot.
  The first implementation requires at least 400 ms remaining before starting a
  ranging master exchange and at least 100 ms remaining before transmitting a
  telemetry beacon.
- The scheduled ranging master broadcasts an endpoint-bearing range success or
  failure report after each exchange. Local-endpoint ranges may feed the solver;
  third-party pair ranges remain network-health evidence only.
- Scheduled telemetry slots carry the normal telemetry beacon; node `0` also
  sends its TDMA timing heartbeat inside its own telemetry slot. Debug telemetry
  remains best-effort per ADR 0004 and must not preempt scheduled ranging.
- TDMA does not shorten the 30 second field evidence window from ADR 0010:
  retained telemetry and range evidence may remain eligible for the solver until
  the existing TTL expires.
- TDMA emits detailed structured field reconstruction logs for slot timing,
  authority synchronization, scheduled action decisions, skipped slots, and
  action outcomes so captures can explain whether failures came from schedule
  alignment or radio exchange behavior. These TDMA logs are always emitted in
  the first field implementation and are not gated by a debug flag.
- TDMA implementation is not field-ready until host tests cover the fixed
  four-node schedule, late-slot skip thresholds, timing-heartbeat validation,
  and 15 second authority expiry; both ESP targets must build before flashing.
- Nodes `1..3` follow node `0` timing instead of independently choosing master
  turns.
- When node `0` is missing, non-authority nodes stay in a visible waiting state
  and listen instead of falling back to independent ranging. Followers declare
  this state after 15 seconds without a valid node `0` timing heartbeat.
- The old staggered/best-effort ranging loop is not the target behavior for
  configured field nodes.
