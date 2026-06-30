# Real GNSS Adapter To Core

## What to build

Replace GPS mock behavior on ESP32 with a real GNSS event path. The adapter
reads NMEA bytes from the ESP32 UART driver, feeds them to the portable NMEA
parser, stamps emitted samples with system time, and injects
`NAV_EVT_LOCAL_GNSS_SAMPLE` into the core when GPS is enabled.

## Acceptance criteria

- [ ] UART NMEA bytes from the GPS module are processed through the portable
      byte-stream parser.
- [ ] Emitted GNSS samples are stamped with system time before core injection.
- [ ] When GPS is disabled through config/control app, local GNSS samples do not
      become the navigation solution source.
- [ ] Valid GNSS fixes update the local snapshot path when forced-denied mode is
      not active.
- [ ] Invalid or no-fix NMEA input is logged or surfaced as diagnostics without
      crashing or allocating dynamically in the portable parser.
- [ ] The control app shows enough local GNSS state to confirm the adapter path.
- [ ] Host parser tests remain deterministic, and hardware evidence is captured
      for at least one GPS-capable board.

## Blocked by

- Working PoC Hardware Integration Gate
