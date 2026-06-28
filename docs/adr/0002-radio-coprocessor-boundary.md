# ADR 0002: Radio Coprocessor Boundary

## Decision

This repository defines only the host-side contract for the radio/ranging
coprocessor. ESP8285/SX1280 firmware is implemented in a separate repository.

## Rationale

Navigation decisions and radio PHY/MAC behavior have different test loops,
hardware requirements, and ownership boundaries.

## Consequences

`core/` receives radio information as events. Protocol payloads and framing can
evolve here, but radio firmware implementation does not live here.
