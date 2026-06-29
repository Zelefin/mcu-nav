# AGENTS.md

## Required Context

Before coding, read `README.md` and the relevant files under `docs/`.

The source of truth for trilateration math is `~/projects/trilateration`.
Read `~/projects/trilateration/AGENTS.md` before changing trilateration-related
code. Prefer the validated portable C solver from that project over rewriting
math.

## Architecture Rules

- Keep `core/` portable C11.
- Do not add ESP-IDF, STM32 HAL, Arduino, FreeRTOS, POSIX, UART driver, or board
  dependencies to `core/`.
- Portable parsers in `core/`, such as the NMEA parser, must remain byte-stream
  adapters with fixed buffers and no file I/O, dynamic allocation, or platform
  APIs.
- Put platform-specific code under `ports/`.
- Do not implement ESP8285/SX1280 radio firmware in this repository.
- Do not start flight-controller integration here; keep only future output
  abstractions and documentation.
- Use event-in, snapshot/log-out flow. The core owns its internal state.
- Treat `nav_system_t` as core-owned state. Application code should normally use
  `nav_core_handle_event()`, `nav_core_tick()`, and `nav_core_get_snapshot()`;
  direct field mutation is for tests or low-level diagnostics.
- Avoid global mutable state and dynamic allocation in the core unless explicitly
  justified.

## Coding Rules

- Use fixed-width integer types and explicit units in names, such as `lat_e7`,
  `alt_mm`, `range_mm`, and `timestamp_ms`.
- Use the `nav_` prefix for public symbols.
- Validate pointers in public functions where reasonable.
- Keep behavior deterministic and tests replayable.
- Treat NMEA timestamps as external adapter time: stamp parsed GNSS samples with
  system/replay `timestamp_ms` before injecting `NAV_EVT_LOCAL_GNSS_SAMPLE`.
- Do not hide important behavior behind vague abstractions.
- Keep functions small and readable; avoid clever macros.

## Test And Documentation Rules

- Add or update tests for behavior changes.
- Update docs when changing architecture, protocol, logging, data models, state
  transitions, or replay behavior.
- Prefer deterministic host tests and replayable scenarios.
- When changing event or data-model fields, update `docs/replay_csv.md`, replay
  fixtures, and replay tests in the same change.
- Treat `examples/replay/` fixtures as regression tests. Do not casually change
  expected outputs; document behavior changes in the fixture or test.
- Keep replay deterministic. Do not add random motion, packet loss, or simulator
  behavior to `nav_replay`.
- Keep simulator behavior under `tools/sim/`; it must generate replay input
  files instead of bypassing replay.
- Keep plotting under `tools/plot/` and generate plots from replay outputs, not
  private simulator state.
- Keep `events.csv`, `replay_config.csv`, `truth.csv`, and expected compare
  reports synchronized when changing replay behavior.
- `truth.csv` is comparison input, not simulator logic. Do not make replay
  generate truth or random scenarios.
- Do not remove logs unless replacing them with better structured logs.
- Keep `docs/radio_protocol.md` synchronized with public protocol enums and
  payload structs. It is a future cross-repo contract with the radio firmware
  repository.
- Keep protocol names precise: `frame_seq` for host-radio frames, `packet_seq`
  for peer beacons, `request_id` for ranging requests, `range_fail_reason` for
  radio failures, and `reject_reason` for navigation decisions.
- Navigation math source of truth remains `~/projects/trilateration`; compare
  against that project before changing solver behavior.
