# ADR 0008: Mappable Position Telemetry Source

## Status

**Accepted** (2026-07).

## Decision

Telemetry beacons carry the node's current accepted mappable position in the existing position fields, whether that position comes from local GNSS or an accepted `RADIO_3D` solution. The beacon must also carry explicit source metadata so receivers can distinguish GNSS positions from estimated radio positions; anchor selection still requires GNSS-valid peer positions and must not use estimated peer positions as navigation anchors.

## Rationale

The control app needs one position stream for map visualization, while the navigation solver needs a stricter anchor contract. Reusing the beacon position fields avoids a parallel estimated-position message, but the explicit source metadata prevents an estimated `RADIO_3D` position from being mistaken for a GNSS anchor.
