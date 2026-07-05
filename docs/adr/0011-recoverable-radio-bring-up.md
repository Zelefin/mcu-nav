# ADR 0011: Recoverable Radio Bring-Up

## Status

**Accepted** (2026-07).

## Decision

ESP radio initialization failures are recoverable during field bring-up. If the
SX1280 BUSY pin stays high before init or `radio.begin(...)` fails, the radio
task keeps reporting `RADIO=FAIL`, waits 5 seconds, pulses the SX1280 reset
line, and retries forever. When a retry succeeds, the node enters the normal
node-ID-staggered ranging startup schedule rather than continuing from the
failed attempt.

## Rationale

Headless field nodes may be powered from an external 5 V rail before USB logging
is attached. In that setup the ESP32, GPS, and SX1280 can settle at different
times; a one-shot radio boot failure makes the node permanently silent until a
manual reset. Retrying forever with an explicit radio reset pulse preserves
visible failure diagnostics for real wiring faults while allowing transient
power/reset timing failures to recover without operator intervention.

## Consequences

- `RADIO=FAIL` means "radio unusable now", not "radio task is dead".
- Persistent wiring or power faults remain visible as repeated failed init logs.
- A recovered node starts ranging with the same staggered schedule as a clean
  boot, reducing collisions when several nodes recover together.
