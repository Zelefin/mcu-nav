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

**Field evidence window**:
A bounded field-test retention window during which the last peer telemetry and
range evidence may remain visible and eligible for the local radio solve after
new packets temporarily stop arriving.
_Avoid_: Permanent cache, replay history

**Field evidence state**:
The operator-facing freshness label for retained field evidence: fresh,
stale, expired, or missing.
_Avoid_: Packet status, solver state

**Mappable node position**:
A node position suitable for operator display, whether it came from local GNSS
or from an accepted radio navigation solution.
_Avoid_: GPS point, anchor

**Estimated node position**:
A mappable node position produced by radio navigation rather than by the node's
own GNSS receiver.
_Avoid_: Fake GPS, anchor, GPS point

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

**Network map**:
The spatial rendering of the network view in the telemetry UI: positioned nodes
plotted on an online geographic map from a single vantage node, with nodes that
lack a usable position listed in an off-map roster. It reads only the peer
snapshot, so a node appears once it has a usable GNSS coordinate or an accepted
navigation solution.
_Avoid_: Radar, minimap, ground station

**Control channel**:
The newline-delimited JSON link over a node's USB-serial port used to read its
snapshot/telemetry and to apply local configuration. It is distinct from the
over-the-air radio protocol.
_Avoid_: Wi-Fi portal, web API, MAVLink

**Control app**:
The browser tool entered through `control-app/index.html` that talks to one node
over the control channel to show the network view and change its configuration.
It may load sidecar field-kit assets such as an offline map bundle.
_Avoid_: Dashboard, GCS, configurator firmware

**Offline map bundle**:
A locally available basemap package for a bounded test area that the control app
can use when internet map tiles are unavailable.
_Avoid_: Tile scrape, global map cache

**Control app field kit**:
A shareable local folder or archive that starts the control app with its offline
map bundle and required browser assets on Linux, Windows, and macOS.
_Avoid_: Installer, cloud dashboard

**This node**:
The navigation node currently connected to the control app over USB serial.
_Avoid_: Browser location, laptop position

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

**Radio solve cadence**:
The maximum rate at which a GPS-disabled node recomputes its radio navigation
solution from retained field evidence. It is separate from GNSS parser rate,
snapshot emission rate, and ranging cycle timing.
_Avoid_: GPS rate, telemetry rate, ranging rate

**GPS-disabled node**:
A node whose local GNSS is not allowed to become its navigation solution or
advertised anchor telemetry. The node may still read GNSS bytes for diagnostics
and can still participate in ranging as a distance-only node.
_Avoid_: GPS-unplugged node, GPS health disabled

**Local GNSS evidence**:
The latest local GNSS fix retained for diagnostics and comparison on a
GPS-disabled node. It is not an accepted navigation solution and must not be
advertised as navigation-anchor telemetry while local GNSS use is disabled.
_Avoid_: Fallback GPS, hidden GPS solution, anchor GNSS

**Working PoC**:
The first verified end-to-end ESP32 setup that passes the hardware integration
gate and proves real ranging, GNSS input, telemetry-UI GPS disable, and
trilateration fallback on the available boards.
_Avoid_: Final product, parallel planning phase

**Distance-only ranging PoC**:
A narrower bring-up step that proves SX1280 distance measurements between
modules without GNSS positions or a radio navigation solution.
_Avoid_: Trilateration, GPS-denied navigation, `RADIO_3D`

**Recoverable radio bring-up**:
An ESP node state where SX1280 initialization failed but the radio task remains
alive, reports `RADIO=FAIL`, and retries initialization until the radio becomes
usable. Once initialization succeeds, the node enters the normal ranging startup
schedule.
_Avoid_: Terminal boot self-test, permanent radio failure

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

**Debug telemetry mode**:
A runtime-only, off-by-default state in which the connected node asks peers over
the radio to broadcast their node quality reports, so one vantage node can
assemble a whole-system quality picture. It never preempts ranging.
_Avoid_: Diagnostics mode, verbose mode, full diagnostics stream

**Node quality report**:
A compact, on-demand, best-effort per-node summary of that node's nav mode,
solution status/source, trilateration-quality metrics, and GNSS health,
broadcast only while debug telemetry mode is active.
_Avoid_: Detailed telemetry, full telemetry, STATS dump

**Debug-enable broadcast**:
The connected node's periodic best-effort packet that keeps peers in debug
telemetry mode for a short TTL; peers auto-revert to off when it stops.
_Avoid_: Poll, command, ping

**Capture session**:
An NDJSON recording of one control-channel session streamed to a disk file for
later viewing and AI analysis. It is not the volatile serial-log view.
_Avoid_: Log dump, trace, session recording
