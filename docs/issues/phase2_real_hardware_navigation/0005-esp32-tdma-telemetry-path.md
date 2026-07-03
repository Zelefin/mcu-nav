# ESP32 TDMA Telemetry Path

## What to build

Connect the ESP32 radio path to the portable telemetry contract. During telemetry
slots, a node encodes and transmits its local telemetry beacon. During listen
periods, received radio frames decode into core peer telemetry events.

## Acceptance criteria

- [ ] The TDMA scheduler selects telemetry transmit, ranging, and listen actions
      from system time.
- [ ] Telemetry transmit slots encode local node telemetry from the current
      snapshot and send it over SX1280 packet mode.
- [ ] Telemetry receive handling decodes incoming beacons and injects peer
      telemetry events into the core.
- [ ] RSSI/SNR from received packets are stored as receive diagnostics, not as
      peer-reported telemetry.
- [ ] The peer table and telemetry UI update from real received beacon data.
- [ ] Bad or unsupported radio frames are rejected without disrupting the node.
- [ ] Host codec tests and at least one two-node hardware telemetry test pass.

## Blocked by

- Working PoC Hardware Integration Gate
- Real GNSS Adapter To Core
