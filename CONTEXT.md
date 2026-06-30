# nav-mcu

This context defines the language for the navigation MCU firmware, its radio
boundary, and hardware bring-up artifacts.

## Language

**Single-hop TDMA network**:
A fixed set of nodes that all directly hear each other and share deterministic
radio time slots without relaying packets through other nodes.
_Avoid_: Mesh, multi-hop mesh

**TDMA time authority**:
The configured node whose heartbeat defines the TDMA frame timing for the
network.
_Avoid_: Coordinator, master

**Navigation anchor**:
A peer whose GNSS-valid position and fresh range can be used as an input to a
radio navigation solve.
_Avoid_: Repeater, relay

**System view**:
The compact per-peer navigation and health table that each node builds from
directly received telemetry, status, and ranging results.
_Avoid_: Dashboard, full diagnostics stream

**Ranging cycle**:
One scheduled pass through the unique peer pairs that should produce fresh
two-way range results for the current TDMA frame plan.
_Avoid_: Mesh update, telemetry cycle

**Adaptive TDMA cycle**:
A scheduled radio cycle that starts at a conservative nominal rate and may move
to a faster configured rate only when recent link health leaves enough guard
time for telemetry and ranging.
_Avoid_: Opportunistic transmit, random backoff

**Network view**:
The whole-network telemetry/health/ranging picture that any single node has
assembled locally from directly received peers, viewable by plugging into that
one node. It is the multi-node face of the per-peer system view.
_Avoid_: Mesh, dashboard, ground station

**Control channel**:
The newline-delimited JSON link over a node's USB-serial port used to read its
snapshot/telemetry and to apply local configuration. It is distinct from the
over-the-air radio protocol.
_Avoid_: Wi-Fi portal, web API, MAVLink

**Control app**:
The single-file browser tool (Web Serial, desktop Chrome) that talks to one node
over the control channel to show the network view and change its configuration.
_Avoid_: Dashboard, GCS, configurator firmware

**NMEA output abstraction**:
A future output boundary that turns an accepted navigation snapshot into
GPS-like NMEA sentences for an external consumer without owning flight-controller
integration or vehicle state.
_Avoid_: Drone integration, MAVLink, flight-controller adapter

**Mock peer source**:
An on-device or host source that injects synthetic peer telemetry and range
events into the navigation core so one physical node can exercise trilateration
without real peers. Toggled at runtime over the control channel.
_Avoid_: Stub, fake radio, simulator (reserve "simulator" for host replay)

**Hardware bring-up example**:
A repository-local example used to validate wiring, chip behavior, and a narrow
hardware capability before production ownership is settled.
_Avoid_: Production firmware, radio firmware home

**Hardware integration gate**:
A required board-level verification point that must pass before the work is
parallelized. It requires four ESP32 nodes flashed and alive, every node
participating in SX1280 ranging smoke, GNSS/NMEA input evidence, and a real
trilateration fallback check after disabling GPS on one node through the control
app.
_Avoid_: Parallel prep gate, production readiness

**Radio navigation acceptance gate**:
The radio-solution portion of the hardware integration gate: an accepted radio
navigation solution from real peer telemetry, fresh SX1280 ranges, and local
altitude after local GPS is disabled on one node.
_Avoid_: Boot self-test, packet-only smoke test

**Working PoC**:
The first verified end-to-end ESP32 setup that passes the hardware integration
gate and proves real ranging, GNSS input, control-app GPS disable, and
trilateration fallback on the available boards.
_Avoid_: Final product, parallel planning phase

**Ranging master**:
The SX1280 ranging role that initiates a ranging exchange and owns the readable
distance result.
_Avoid_: Initiator, laptop module

**Ranging slave**:
The SX1280 ranging role that waits for a matching ranging request and sends the
automatic ranging response.
_Avoid_: Responder, powered-only module

**Ranging address**:
The 32-bit SX1280 address embedded in a ranging request that decides which
ranging slave may answer the exchange.
_Avoid_: Node ID, peer ID, frame sequence

**Corrected range**:
The human-facing distance after applying short-range compensation to the raw
SX1280 ranging result.
_Avoid_: Raw range, register result

**Uncorrected range**:
The human-facing distance reported directly from SX1280 ranging conversion
without empirical short-range compensation.
_Avoid_: Corrected range, calibrated distance

**Successful ranging exchange**:
A ranging exchange where the master receives a valid SX1280 result and logs raw
and uncorrected distance values.
_Avoid_: Accurate distance, calibrated result

**Master ranging log**:
The primary diagnostic line emitted by the ranging master for each attempt,
including success, attempt number, uncorrected meters, raw register value, and
RadioLib error details.
_Avoid_: Slave heartbeat, health summary

**Standalone ranging slave**:
A ranging slave powered without a serial monitor after role selection; master
logs are the primary evidence that it is responding.
_Avoid_: Headless master, unobservable exchange

**Boot-window role selection**:
A startup window where pressing BOOT/GPIO0 selects ranging slave, while no
press selects ranging master.
_Avoid_: Bootloader mode, separate slave firmware

**Shared ranging profile**:
The fixed SX1280 settings both boards must use for a ranging exchange:
2445 MHz, SF7, 1625 kHz bandwidth, coding rate 4/5, and ranging address
0x53423234.
_Avoid_: Health-check LoRa profile, per-board radio settings

**Ranging exchange**:
One master-initiated SX1280 request/response attempt that ends in either a
readable master result or a reported failure.
_Avoid_: Sample window, navigation range result

**RF front-end path**:
The board-level transmit or receive signal path selected outside the SX1280
chip before a ranging exchange.
_Avoid_: Ranging role, distance correction

**Single-image ranging firmware**:
One firmware image that can become either ranging master or ranging slave at
boot based on a local role-selection input.
_Avoid_: Master build, slave build

**External RF front-end control**:
Board-specific GPIO control for PA/LNA transmit and receive paths outside the
SX1280 chip.
_Avoid_: SX1280 ranging direction, LoRa packet mode

**ESP32-S3 ranging example**:
A hardware bring-up example under `examples/esp32s3-ranging` that ports the
short-range SX1280 ranging workflow to ESP32-S3 plus an E28-2G4M12SX module.
_Avoid_: Production radio firmware, navigation-core ranging

**Self-contained example**:
A hardware bring-up example with its own PlatformIO project files, source, and
README under its `examples/` directory.
_Avoid_: Root firmware mode, shared application target
