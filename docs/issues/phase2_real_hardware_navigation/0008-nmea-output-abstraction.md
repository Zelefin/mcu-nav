# NMEA Output Abstraction

## What to build

Add a future output boundary that turns an accepted navigation snapshot into
GPS-like NMEA sentences for an external consumer. This is not flight-controller
integration; it is a deterministic output abstraction plus an ESP32 UART smoke
path.

## Acceptance criteria

- [ ] Accepted navigation snapshots can be encoded as a minimal GPS-like NMEA
      stream.
- [ ] The initial sentence set includes enough data for a GPS replacement
      stream, with deterministic checksums.
- [ ] No flight-controller-specific state machine, MAVLink behavior, or
      vehicle-specific integration is added.
- [ ] The output path can be enabled or disabled through configuration.
- [ ] Host tests verify sentence content, checksums, no-solution behavior, and
      deterministic output.
- [ ] ESP32 UART smoke evidence shows the stream being emitted from a board.
- [ ] Documentation states limitations and wiring/config expectations.

## Blocked by

- Radio Navigation Acceptance On Real Boards
