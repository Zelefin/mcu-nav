# ADR 0003: Logging-First Design

## Decision

Structured text logs and CSV streams are first-class outputs from the beginning.

## Rationale

GPS-denied navigation failures are usually data-quality or geometry problems.
Debugging requires replayable input events, per-anchor acceptance reasons, and
solution diagnostics.

## Consequences

Changes to data models, state decisions, protocol messages, or solver behavior
must update logs and docs. Logs should be removed only when replaced with better
structured diagnostics.
