# Radio Navigation Acceptance On Real Boards

## What to build

Turn the Working PoC into a repeatable radio navigation acceptance scenario.
Using real ESP32 boards, real peer telemetry, fresh SX1280 ranges, and local
altitude, disable GPS on one node through the control app and observe an
accepted `RADIO_3D` solution.

## Acceptance criteria

- [ ] At least three anchor nodes provide GNSS-valid peer telemetry.
- [ ] The GPS-disabled local node has fresh valid ranges to usable anchors.
- [ ] The local node has a fresh valid local altitude source.
- [ ] The control app disables local GPS and the resulting solution source is
      not local GNSS.
- [ ] The local node reaches an accepted radio navigation solution with
      residual and geometry diagnostics visible.
- [ ] Failure cases are diagnosable as navigation `reject_reason` values, not
      confused with radio `range_fail_reason` values.
- [ ] Evidence includes control-app state, serial logs, board identities, and
      final navigation status.
- [ ] The debug playbook or development docs describe how to repeat the
      acceptance scenario.

## Blocked by

- Real GNSS Adapter To Core
- ESP32 TDMA Telemetry Path
- ESP32 SX1280 Ranging Slots
- Pair Range Network View
