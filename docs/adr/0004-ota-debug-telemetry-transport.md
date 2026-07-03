# ADR 0004: On-Demand Debug Telemetry Transport

## Status

**Accepted** (2026-07).

## Context

A connected node can only build its own **network view** from peers it directly
hears. It cannot see each remote node's own solution/GNSS quality — the data most
needed to analyze **trilateration quality** across the whole system. We want an
operator to enable a **debug telemetry mode** on one node, have peers report a
compact **node quality report** over the air, capture the stream, and analyze it
offline (see `docs/prd_ota_debug_telemetry_capture.md`).

The user requirement is explicit: this extra telemetry must be **lower priority
than distance measurement and trilateration** and **off by default**.

Implementing this radio behavior in the nav-node firmware is consistent with ADR
0002's superseding decision (the SX1280 is driven directly from the nav MCU and
`nav_radio_protocol` is already the over-the-air packet format). The `AGENTS.md`
line "Do not implement ESP8285/SX1280 radio firmware in this repository" predates
that supersession and is **stale**; it should be reconciled with ADR 0002.

## Decision

1. **Transport: best-effort broadcast, not dedicated TDMA slots.** Node quality
   reports and the debug-enable signal are transmitted best-effort in the radio
   task's idle listen window, reusing the existing packet TX/RX path (the `NRR1`
   report mechanism) with a message-type discriminator and the typed
   `nav_radio_protocol` / `nav_telemetry` payloads. Transmission is gated behind
   "enough guard time before the next ranging action" so ranging master scans and
   slave listen keep their current cadence.

2. **Lifetime: runtime-only with an OTA auto-timeout.** The `debug` control
   command sets a RAM-only flag that is **never persisted to NVS**, so every node
   powers up with debug off. The connected node periodically emits a
   **debug-enable broadcast** carrying a TTL; a peer enters debug telemetry mode
   only while it keeps hearing that broadcast and **auto-reverts** when the TTL
   lapses. "Off by default" is therefore structural and self-healing.

## Considered options

- **Dedicated TDMA telemetry slots** (wire up `nav_tdma` on hardware, reserve
  guard-time slots for telemetry). Rejected for now: it reserves air time whether
  or not it is needed, contradicts "lower priority than ranging," and requires
  building the scheduled TDMA task that does not yet run on hardware.
- **Persisted debug flag (NVS).** Rejected: a node could silently boot still in
  debug and keep transmitting, contradicting "off by default."
- **Ad-hoc text broadcast** (extend the `NRR1` string scheme). Rejected as the
  contract: it perpetuates an untyped protocol and diverges from the designed,
  host-tested `nav_radio_protocol` payloads.

## Consequences

- The dormant `nav_radio_protocol` / `nav_telemetry` codec becomes live on
  hardware for the new messages; keep `docs/radio_protocol.md` synchronized with
  the enums/payloads (per ADR 0003, logging/diagnostics are first-class).
- Best-effort delivery means node quality reports can be lost; consumers treat
  them as diagnostics with freshness, never as anchor inputs.
- Reconcile the stale `AGENTS.md` radio-firmware line with ADR 0002 when this
  lands.
