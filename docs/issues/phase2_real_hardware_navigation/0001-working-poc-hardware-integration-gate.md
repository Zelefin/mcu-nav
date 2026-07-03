# Working PoC Hardware Integration Gate

## What to build

Produce the first verified end-to-end ESP32 Working PoC. This is a serial
hardware integration task done by one person before the rest of the backlog is
parallelized.

The PoC must prove that four ESP32 nodes can be flashed, booted, and observed;
that every node participates in SX1280 ranging smoke; that GNSS/NMEA input is
visible on GPS-equipped nodes; and that one node can have GPS disabled through
the telemetry UI and still produce a real trilateration fallback from peer
telemetry, fresh ranges, and local altitude.

Current implementation starts with a narrower Distance-only ranging PoC:
persisted node IDs are configured through the telemetry UI, each node alternates
between addressed slave listening and active master scans, and no GNSS positions
or `RADIO_3D` solution are required for this first radio-distance check. GPS
input on ESP32 boards now feeds the portable NMEA parser and core when connected;
boards without GPS still participate as distance-only nodes.

## Acceptance criteria

- [ ] Two `esp32-s3-devkitc-1` nodes and two `nodemcu-32s` nodes are flashed
      with the root firmware and boot with identifiable serial output.
- [ ] Each node has evidence for SX1280 initialization.
- [ ] Every board participates in at least one successful SX1280 ranging smoke
      exchange.
- [ ] Ranging smoke evidence includes at least one `esp32-s3-devkitc-1` to
      `nodemcu-32s` exchange.
- [ ] GNSS/NMEA input evidence exists on GPS-equipped nodes. Valid fix is
      preferred; NMEA bytes without fix may be recorded separately as wiring
      evidence when testing indoors.
- [ ] The telemetry UI can connect to a node and disable GPS for that node.
- [ ] With GPS disabled on one node, the system demonstrates trilateration
      fallback from real peer telemetry, fresh SX1280 ranges, and local
      altitude.
- [ ] Evidence is captured as a small matrix of board, firmware env, observed
      radio status, observed GNSS/NMEA status, telemetry-UI action, and final
      navigation outcome.
- [ ] Any firmware changes made only to pass this gate are documented as
      bring-up changes, not hidden as permanent architecture.

## Distance-only acceptance subset

- [ ] Each board can be assigned a unique persisted node ID from the telemetry UI.
- [ ] A node logs successful `range_result ok=true from=<self> to=<peer>` lines
      for at least one powered peer while other nodes log slave responses.
- [ ] The telemetry UI can connect to any node and discover observed nodes and
      single-hop pairs from received serial telemetry, not from static UI rows.
- [ ] The telemetry UI distance-only view renders missing GPS-derived peer fields
      as `—`, not as misleading zero coordinates.
- [ ] The distance-only result is documented as ranging evidence only, not as
      trilateration or `RADIO_3D`.

## Blocked by

None - this is the first required task.
