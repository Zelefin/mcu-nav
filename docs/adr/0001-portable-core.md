# ADR 0001: Portable Core

## Decision

The navigation core is plain C11 with no direct dependency on RTOS, HAL,
drivers, POSIX, or board APIs.

## Rationale

The same navigation behavior must run in host tests, simulator/replay, ESP32
firmware, and any future ports. A portable core keeps decisions testable and
comparable.

## Consequences

Ports must translate platform inputs into `nav_event_t` and consume snapshots or
logs. Core code cannot call platform APIs directly.
