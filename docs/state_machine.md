# State Machine

```mermaid
stateDiagram-v2
    [*] --> BOOT
    BOOT --> GNSS_OK: local GNSS usable and not forced denied
    BOOT --> NO_NAV_SOLUTION: no GNSS and radio solve unavailable
    GNSS_OK --> DEMO_FORCED_DENIED: force denied command/config
    DEMO_FORCED_DENIED --> RADIO_NAV_OK: 3 anchors + altitude + accepted solve
    DEMO_FORCED_DENIED --> RADIO_NAV_DEGRADED: solve accepted with marginal quality
    DEMO_FORCED_DENIED --> NO_NAV_SOLUTION: missing altitude, anchors, geometry, or solver failure
    GNSS_OK --> NO_NAV_SOLUTION: GNSS lost and radio solve unavailable
    NO_NAV_SOLUTION --> RADIO_NAV_OK: radio solve becomes available
    RADIO_NAV_OK --> NO_NAV_SOLUTION: anchors/ranges/altitude expire
    RADIO_NAV_OK --> GNSS_OK: forced denied off and GNSS usable
```

## Mode Rules

- `GNSS_OK`: local GNSS is usable and `demo_force_gps_denied` is false.
- `DEMO_FORCED_DENIED`: local GNSS may be logged as truth/debug, but it is not
  used as the position source.
- `RADIO_NAV_OK`: three anchors, fresh local altitude, acceptable geometry, and
  trilateration succeeded with acceptable quality.
- `RADIO_NAV_DEGRADED`: solution exists, but v1 quality or geometry is below the
  preferred threshold while still above the hard reject threshold.
- `NO_NAV_SOLUTION`: no accepted local or radio solution is available.

`GPS_DENIED` and `GNSS_SUSPECT` remain reserved for richer GNSS health logic.
The current forced-denied path is enough for deterministic radio navigation
tests and demos.
