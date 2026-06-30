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

**Mock peer source**:
An on-device or host source that injects synthetic peer telemetry and range
events into the navigation core so one physical node can exercise trilateration
without real peers. Toggled at runtime over the control channel.
_Avoid_: Stub, fake radio, simulator (reserve "simulator" for host replay)
