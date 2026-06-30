# ESP32 Documentation Refactor Plan

This is a local refactor plan. It is not tracked as a GitHub issue.

## Problem Statement

The repository documentation still mixes older platform assumptions with the
current codebase.

STM32-specific future-port wording remains even though the STM32 port has been
removed. Some active architecture text still describes the old radio
coprocessor boundary even though the recorded direction is now on-device SX1280
radio, with node-to-node over-the-air packets. The public docs also blur the
line between the root ESP-IDF navigation-node firmware, the self-contained
ESP32-S3 ranging bring-up example, and the SpeedyBee ESP8285 debug target.

This makes the project harder to navigate because contributors cannot quickly
tell:

- Which hardware path is primary now.
- Whether STM32 is still planned or supported.
- Whether radio traffic is host-to-coprocessor or over the air between nodes.
- Which ESP32 hardware work is already present and which integration work is
  still future.
- Whether SpeedyBee is a primary target or a secondary/debug target.
- How third-party pair ranges will be represented so the control app can show a
  whole-network range view, not only local-to-peer distances.

## Solution

Actualize the documentation around the current ESP32 focus:

- Make ESP32 / ESP32-S3 the primary hardware path in public docs.
- Remove active STM32 roadmap language.
- Keep the portable-core rule: no platform dependencies in `core/`.
- Make the SX1280 ranging engine the required source of node-to-node distance
  measurements. TDMA schedules the ranging slots; it does not estimate distance
  from packet timing or RSSI.
- Extend planned range protocol payloads from single `peer_id` semantics to
  endpoint-bearing `from_id` / `to_id` semantics so any node can report and
  display third-party pair ranges.
- Keep SpeedyBee documented only as the existing secondary ESP8285/SX1280 debug
  target unless the project separately decides to remove it.
- Add a root `CHANGELOG.md` so every PR has a concise, human-readable summary
  of behavior, protocol, docs, test, and migration changes.
- Preserve historical context only where it is explicitly historical, such as a
  superseded ADR.
- Distinguish clearly between:
  - portable C navigation core and host tools,
  - root ESP-IDF navigation-node firmware,
  - self-contained ESP32-S3 SX1280 ranging bring-up example,
  - secondary SpeedyBee debug/reference material.

The docs should not overstate current integration. The ESP-IDF firmware has
radio/GPS/compass health checks, control channel, persisted config, mock peer
source, and PlatformIO builds. Real GPS and radio data are not fully wired into
the navigation core event flow yet.

## Current Session Status

The first documentation pass has already updated the active docs to match the
ESP32/SX1280 direction:

- `README.md` now describes the ESP32 TDMA/ranging path, the SX1280 ranging
  engine requirement, and the planned `from_id` / `to_id` pair-range payloads.
- `TASKS.md` now treats ESP32 radio/GPS event integration as the active Phase 2
  work, points ranging slots at `examples/esp32s3-ranging`, and adds the
  pair-range store/control-app step.
- `docs/architecture.md`, `docs/data_flow.md`, `docs/data_model.md`,
  `docs/development_plan.md`, `docs/radio_protocol.md`, `docs/replay_csv.md`,
  and `docs/logging.md` now distinguish current `peer_id` local-to-peer replay
  data from the planned endpoint-bearing TDMA contract.
- `ports/esp32s3/README.md` now describes the root ESP-IDF firmware split and
  the expected ESP32 ranging integration responsibilities.
- `examples/esp32s3-ranging/README.md` now maps the example's master/slave roles
  to `from_id` / `to_id` for production TDMA planning.
- `ports/speedybee/reference/RANGING_README.md` now frames SpeedyBee ranging as
  reference/secondary material instead of the primary implementation path.
- `CHANGELOG.md` now exists with an `Unreleased` section for this documentation
  refactor, and `CONTRIBUTING.md` now tells contributors when to update it.

No C API, replay fixture, telemetry codec, or control-app behavior has been
changed yet. The next implementation work must keep `docs/radio_protocol.md`,
`docs/replay_csv.md`, fixtures, and tests synchronized when `from_id` / `to_id`
become real event/protocol fields.

## Commits

1. Audit stale platform wording.

   Search all root markdown, `docs/`, ADRs, port notes, example READMEs, and
   contributor docs for STM32, old SB24TX example paths, radio-coprocessor
   wording, target-count language, and future-host-port wording. Classify every
   hit as either active guidance, historical context, or a valid core guardrail.
   This commit should only record the audit notes if needed; it should not
   change behavior.

2. Update the top-level project overview.

   Rewrite the opening sections so the primary hardware story is ESP32 /
   ESP32-S3 navigation-node firmware over the portable core. Keep host replay
   and simulation visible. Remove language that says this repository owns
   "future host ports" in a way that implies STM32 is still expected.

3. Refresh supported hardware wording.

   Keep the current PlatformIO targets accurate. Present `nodemcu-32s` and
   `esp32-s3-devkitc-1` as the primary ESP-IDF firmware targets. Present
   `speedybee` as a secondary/debug target if it remains supported. Avoid
   phrasing that treats all three targets as equal project focus.

4. Correct current status and next milestones.

   Replace stale milestone text that says to add ESP32-S3/STM32 host adapters.
   The next docs should say the current integration target is ESP-IDF GPS/radio
   adapters that feed `nav_event_t` into the core, plus real on-device SX1280
   telemetry and SX1280 ranging-engine integration. The docs should point to
   `examples/esp32s3-ranging` as the proven ESP32-S3/RadioLib workflow for the
   ranging slot.

5. Align architecture docs with on-device radio.

   Rewrite active architecture sections so they describe on-device radio drivers
   feeding events into the portable core. Remove active "host-side radio
   coprocessor contract" language. Keep radio-coprocessor wording only inside
   historical or superseded ADR context.

6. Document the ranging-engine requirement.

   Update active radio/data-flow documentation so distance measurement is
   defined as an SX1280 ranging-engine exchange. Telemetry slots use normal
   packet TX/RX. Ranging slots switch the radio into SX1280 ranging mode:
   ranging master calls the equivalent of RadioLib `startRanging(true, ...)`,
   the peer listens with `startRanging(false, ...)`, and the readable master
   result becomes `NAV_EVT_RANGE_RESULT`. RSSI/SNR remain link diagnostics, not
   distance inputs.

7. Document endpoint-bearing range payloads.

   Add the protocol/data-model requirement that range results and range failures
   carry both endpoints: `from_id` and `to_id`. `from_id` is the scheduled
   ranging initiator / SX1280 ranging master for that exchange, and `to_id` is
   the scheduled ranging peer / SX1280 ranging slave. The measured distance is a
   pair observation between those two nodes.

   Preserve the navigation rule that the core may use a range as a local anchor
   only when one endpoint is the local node. Ranges where neither endpoint is the
   local node are third-party pair observations: store them for network health,
   diagnostics, replay, and control-app display, but do not silently feed them
   into the current three-anchor local solver.

8. Plan the pair-range view.

   Extend the implementation plan so the core or app layer stores recent
   pair-range observations separately from the per-peer anchor fields. The
   control-channel JSON should expose those pair ranges as a small range matrix
   or `ranges[]` list with `from_id`, `to_id`, `range_mm`, freshness, validity,
   RSSI/SNR, request id, and failure reason. The control app should render these
   as network-health data in addition to the existing peer table.

9. Update the portable-core ADR rationale.

   Keep the C11 portable-core decision. Replace the fixed target list that names
   STM32 with durable wording: behavior must run in host tests, replay/sim,
   ESP32 firmware, and any future ports.

10. Update GNSS/NMEA docs.

   Replace limitation text that says there is no ESP32-S3 or STM32 UART driver.
   The accurate status is narrower: the portable NMEA parser exists, ESP-IDF GPS
   health reading exists, and the hardware-to-core event adapter remains
   future/in-progress.

11. Update replay docs.

   Remove STM32 from the list of hardware not required for replay. Use durable
   wording such as "real hardware" or "platform firmware" instead of naming
   inactive ports. When implementing endpoint-bearing ranges, update replay CSV
   input/output schemas and fixtures in the same change so pair ranges are
   replayable and deterministic.

12. Refresh ESP32-S3 port notes.

   Replace placeholder text saying the ESP32-S3 host port is entirely future.
   Explain the current split: root ESP-IDF firmware contains the board bring-up
   and control-channel work, while a cleaner port layout or deeper hardware
   event integration can be future work. The notes should state that ESP32-S3
   ranging integration should reuse the SX1280 ranging-engine workflow proven by
   the self-contained example.

13. Refresh contributor build guidance.

   Update verification wording so ESP32 builds are the primary firmware check.
   If SpeedyBee remains in scope, keep its build command but call it secondary
   or debug-target verification. Keep host CMake/CTest checks unchanged.

14. Refresh example/reference docs.

   Fix references to removed `examples/sb24tx-ranging` paths. Mark SpeedyBee
   ranging material as reference or secondary bring-up material. Keep the
   ESP32-S3 ranging example clearly labeled as self-contained hardware bring-up,
   not production navigation firmware.

15. Create the project changelog.

   Add a root `CHANGELOG.md` with a compact format that the team can update in
   every PR. Start with an `Unreleased` section and small categories such as
   `Added`, `Changed`, `Fixed`, `Removed`, `Protocol`, `Docs`, and `Migration`.
   Include a short note in contributor or task documentation that PRs changing
   behavior, protocol/data models, replay fixtures, build targets, or user-facing
   docs should update the changelog in the same PR.

   Keep changelog entries practical: one or two lines per meaningful change,
   written for teammates reviewing and operating the firmware, not as a raw git
   commit log.

16. Re-run text consistency checks.

   Search for active STM32 references and confirm any remaining hits are only
   guardrails or historical notes. Search for radio-coprocessor wording and
   confirm it appears only in superseded/historical context. Search for removed
   example paths and confirm no build instructions point to missing directories.
   Search for range/distance wording and confirm it names the SX1280 ranging
   engine rather than RSSI or software packet timing. Search range payload
   wording and confirm the planned real-air schema uses `from_id` / `to_id`, not
   ambiguous `peer_id`.

17. Run lightweight verification.

   For a documentation-only change, verify markdown links and command blocks by
   inspection plus text searches. If build instructions are changed materially,
   run the relevant host or PlatformIO command when practical.

## Decision Document

- The primary public documentation path is ESP32 / ESP32-S3.
- STM32 is not an active roadmap target.
- The portable core remains platform-free.
- Active radio docs describe on-device SX1280 and over-the-air node packets.
- Node-to-node distance measurements come from the SX1280 ranging engine.
  Packet RSSI/SNR and packet timing are diagnostics/scheduling signals, not
  distance-estimation sources.
- Real-air range payloads should identify both endpoints with `from_id` and
  `to_id`. `peer_id` is only sufficient for old local-to-peer fixtures and must
  not be the long-term air contract for TDMA ranging.
- Third-party pair ranges are network-view diagnostics unless/until the solver
  explicitly supports constraints between non-local nodes.
- Old radio-coprocessor language is historical context only.
- Root ESP-IDF firmware exists today, but real GPS/radio event integration into
  the navigation core remains incomplete.
- The ESP32-S3 ranging example is hardware bring-up, not production navigation
  firmware.
- SpeedyBee remains secondary/debug documentation unless a separate code-removal
  decision is made.
- Keep inactive platform names out of roadmap text. Use "future ports" when the
  point is portability rather than a specific board commitment.
- `CHANGELOG.md` should be the PR-level communication surface for notable
  changes. It should not replace detailed docs, ADRs, replay fixtures, or test
  evidence.

## Testing Decisions

- This is documentation refactoring, so the primary tests are consistency checks
  over externally visible docs.
- A good verification pass proves active STM32 roadmap language is gone, while
  valid guardrail wording against platform dependencies can remain.
- A good verification pass proves active radio-coprocessor language is gone
  outside historical ADR context.
- A good verification pass proves no command block points to removed example
  directories.
- A good verification pass proves active distance/ranging docs require the
  SX1280 ranging engine.
- A good protocol test will round-trip a range result where the local receiver
  is neither endpoint and prove that `from_id`, `to_id`, `request_id`, range,
  validity, RSSI/SNR, and failure reason survive decode.
- A good control-channel test will serialize pair ranges separately from the
  per-peer anchor table so the control app can render third-party pair ranges.
- A good changelog check proves every PR that changes externally visible
  behavior, protocol/data-model shape, replay fixtures, build targets, or public
  documentation updates `CHANGELOG.md`.
- Existing host C tests and replay tests are not directly affected unless the
  documentation work is paired with behavior changes, which is out of scope.

Suggested checks:

```bash
rg -n -i "stm32|radio coprocessor|coprocessor|sb24tx-ranging|examples/sb24tx" \
  README.md CONTRIBUTING.md CHANGELOG.md CONTEXT.md TASKS.md AGENTS.md docs ports examples tools \
  -g '*.md'

rg -n -i "rssi.*distance|distance.*rssi|packet timing|time-of-arrival|ranging engine|startRanging" \
  README.md CHANGELOG.md docs TASKS.md ports examples -g '*.md'

rg -n -i "peer_id.*range|range.*peer_id|from_id|to_id|third-party|pair range" \
  README.md CHANGELOG.md docs TASKS.md ports examples -g '*.md'

test -f CHANGELOG.md

cmake -S . -B build
cmake --build build
ctest --test-dir build --output-on-failure
```

Run PlatformIO builds only if the changed documentation alters firmware build
instructions or target scope:

```bash
pio run -e nodemcu-32s
pio run -e esp32-s3-devkitc-1
pio run -e speedybee
```

## Out of Scope

- Removing SpeedyBee code, PlatformIO environments, or reference material.
- Moving root ESP-IDF firmware into a port directory.
- Implementing ESP-IDF GPS-to-core event adapters.
- Implementing real SX1280 telemetry/ranging integration for navigation.
- Replacing the SX1280 ranging engine with RSSI-based or software packet-timing
  distance estimation.
- Changing trilateration solver behavior to consume third-party pair ranges as
  constraints.
- Changing trilateration math or solver behavior.
- Backfilling a full historical changelog from old commits. The initial
  `CHANGELOG.md` can start at `Unreleased` and cover changes from this refactor
  forward.

## Further Notes

The scan found no remaining STM32 source tree. After the current documentation
pass, remaining STM32/coprocessor references should be limited to historical
context, superseded ADRs, local refactor-plan notes, and core guardrails against
platform dependencies.

The radio protocol document now states the SX1280 ranging-engine requirement and
the planned endpoint-bearing payload direction. Implementation is still pending:
the C structs, telemetry codec, replay CSV schema, fixtures, serial JSON, and
control app still need a coordinated `from_id` / `to_id` changeset.

The docs should keep the distinction between "platform dependency forbidden in
core" and "platform not supported by this repo." Mentions of STM32 HAL as a
forbidden core dependency can remain if they are guardrails rather than roadmap
language.
