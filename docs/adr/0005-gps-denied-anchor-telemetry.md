# ADR 0005: GPS-Denied Anchor Telemetry

## Status

**Accepted** (2026-07).

## Context

The system must support GPS-denied navigation for a local node while other nodes
can still provide their current GNSS coordinates and heights. The hardware mix is
not uniform:

- NodeMCU-32S and ESP32-S3 can have an HGLRC M100 GPS connected.
- SpeedyBee Nano 2.4G never has GPS in this project.
- A GPS-capable board may also run with no GPS connected or with GPS usage
  disabled from `telemetry-ui/`.

The portable solver is the validated altitude-constrained three-anchor C solver
from the `trilateration` project. It needs three anchor positions, three local
ranges, and a local altitude constraint.

## Decision

1. ESP32 GNSS-capable firmware reads NMEA over UART, stamps parsed samples with
   local time, and injects `NAV_EVT_LOCAL_GNSS_SAMPLE` into the portable core.
2. A node transmits an OTA beacon with coordinates when it has a current
   accepted mappable position. That position may be local GNSS or an accepted
   `RADIO_3D` estimate. The beacon carries explicit source/status metadata so
   receivers can distinguish a GNSS anchor from a no-GPS estimate.
3. SpeedyBee remains a radio-only firmware target. It consumes peer coordinate
   beacons and participates in ranging, but it never advertises GNSS coordinates.
4. GPS-disabled mode means local GNSS cannot become the navigation solution or an
   advertised anchor. The local node can still solve `RADIO_3D` from peer GNSS
   anchors, SX1280 ranges, and a fresh local altitude sample, and may advertise
   that estimate for map display with `gnss_valid = false`.
5. Anchor selection uses the peer coordinate that was current when a local range
   was accepted. Later beacons refresh diagnostics, but they do not get combined
   with older distances until a newer range arrives.
6. Compass input is out of scope and removed from firmware and documentation.

## Consequences

- A real GPS-denied `RADIO_3D` solve requires three GPS-valid peer anchors, three
  fresh local ranges to those peers, and a fresh local altitude sample.
- `gnss_valid` remains the anchor-eligibility signal. A peer beacon with
  `solution_source = RADIO_3D` can be rendered on a map, but it must not be used
  as another node's GNSS anchor.
- A one-table hardware setup is useful for compile, telemetry, and packet
  plumbing, but short-distance SX1280 range failures are not treated as final
  navigation acceptance evidence. Larger-area tests at roughly 100-500 m are the
  meaningful RF/ranging validation.
- `docs/radio_protocol.md`, telemetry UI labels, and replay/debug docs must stay
  aligned with the distance-only and GPS-anchor distinction.
