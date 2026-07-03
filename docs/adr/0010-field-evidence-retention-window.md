# ADR 0010: Field Evidence Retention Window

## Status

**Accepted** (2026-07).

## Decision

For the next four-node outdoor field test, the ESP32 hardware control-channel
configuration uses a 30 second `telemetry_ttl_ms` and a 30 second
`range_ttl_ms`. The control app uses the same field evidence window for display:
evidence is fresh for the first 5 seconds, stale from 5 to 30 seconds, and
expired after 30 seconds.

Retained evidence remains eligible for the existing radio navigation solve until
the core TTL expires. New packets replace the retained peer telemetry or range
evidence immediately. The trilateration solver itself is unchanged.

The control app exposes this retention explicitly through a field checklist,
range ages, and stale marker styling. Local-to-peer map range links are shown
with distance and age; peer-to-peer ranges remain in tables and captures.

## Rationale

The real hardware setup may deliver useful GPS and ranging evidence more slowly
than the earlier narrow TTLs allowed, especially while four nodes are sharing
debug telemetry, beacons, and scheduled ranging. A 30 second retention window
keeps the operator view useful during short packet gaps and gives the field team
time to see whether the anchor triangle is forming.

The cost is that stale evidence can be wrong while people are walking. The UI
therefore labels freshness rather than hiding the tradeoff behind a single
ready/not-ready state.

## Consequences

- Tomorrow's ESP32 field build is more tolerant of brief radio gaps.
- A `RADIO_3D` solve may use stale but not expired peer/range evidence.
- Operators should pause after moving nodes before judging solve accuracy.
- Debug telemetry remains a manual toggle, because leaving it on can add radio
  traffic during final solve checks.
- The TTL decision should be revisited after field data shows the real beacon
  and ranging cadence.
