# Changelog

Notable project changes are tracked here so each PR leaves a concise summary
for teammates reviewing, testing, and operating the firmware.

Use the `Unreleased` section for PRs in flight. Keep entries short and focused
on externally visible behavior, protocol/data-model changes, replay fixtures,
build targets, migration notes, and documentation that changes how contributors
work.

## Unreleased

### Added

- Added this changelog as the PR-level summary surface for notable changes.
- Added a Phase 2 real hardware navigation PRD that makes the Working PoC and
  Hardware integration gate the serial blocker before parallel contribution
  work begins.
- Added a distance-only SX1280 ranging PoC in the root ESP32 firmware: each node
  alternates between addressed slave listening and active master scans to
  measure single-hop links without GNSS.
- Added a persisted `node_id` control command and control-app field so one
  firmware image per board type can be assigned node IDs `0..3` at runtime.
- Added a control-app distance observation table that combines peer snapshots
  with parsed `range_result` serial logs from the connected node.
- Added a telemetry-discovered distance-only control-app view for observed node
  pairs and peer rows, with GPS-derived fields rendered as `—` while GNSS
  telemetry is absent.
- Added best-effort ESP32 range-report broadcasts so a control app connected to
  one node can discover pair observations measured by other nodes as
  `source=air_report` diagnostics.
- Added a control-app node-name table that caches labels for nodes `0..3` and
  persists the connected node's name through the existing NVS-backed name
  command.
- Added on-demand OTA debug telemetry: `DEBUG_ENABLE` broadcasts keep peers in
  runtime-only debug telemetry mode with a TTL, and peers broadcast compact
  `NODE_QUALITY_REPORT` diagnostics over the best-effort radio path.
- Added typed NDJSON control-channel records for `snapshot`, `node_quality`,
  `range`, and `log`, plus a RAM-only `debug` serial command.
- Added a SpeedyBee SX1280 packet bridge that can hear `DEBUG_ENABLE` over the
  air and broadcast `NODE_QUALITY_REPORT` diagnostics while debug telemetry is
  active.
- Added an issue-shaped Phase 2 backlog under `docs/issues/` with acceptance
  criteria for the Working PoC, endpoint-bearing ranges, pair-range network
  view, GNSS adapter, ESP32 TDMA/ranging paths, radio navigation acceptance,
  NMEA output, and optional SpeedyBee debug work.

### Changed

- Actualized documentation around the ESP32/ESP32-S3 navigation-node focus,
  on-device SX1280 ownership, and secondary SpeedyBee ranging reference.
- Updated the domain glossary with Working PoC, Hardware integration gate,
  Radio navigation acceptance gate, Distance-only ranging PoC, and NMEA output
  abstraction terminology.
- Linked the ESP32 hardware integration milestone to the new PRD and issue
  backlog.
- Replaced the root firmware's packet radio health loop with the distance-only
  single-hop ranging discovery loop for current hardware bring-up.
- Switched the ESP32-S3 DevKitC default console to primary USB Serial/JTAG so
  `/dev/cu.usbmodem*` supports both snapshot output and inbound control JSON.
- Disabled GPS and compass runtime health tasks for the distance-only ESP32
  firmware slice; serial health now reports `GPS=DISABLED` and
  `COMPASS=DISABLED` instead of peripheral failures.
- Changed the control-app distance table to render all pair combinations from
  discovered nodes, mark missing observations explicitly, and render absent
  numeric diagnostics as `—` instead of `0.0`.
- Changed the control-app to display failed SX1280 `uncorrected_m` diagnostics
  as red distance values for `invalid distance` bring-up cases, while valid
  ranges remain green.
- Reflowed the control-app distance and peer tables to fit without horizontal
  scrolling, with range failure notes exposed as an info icon beside non-ok
  distance states.
- Changed repository-root firmware serial output from bare snapshots and raw
  text logs to typed NDJSON records documented in `docs/capture_ndjson.md`.
- Changed the SpeedyBee firmware serial output to the same typed NDJSON
  `snapshot` / `node_quality` / `log` envelope and migrate stale node id `5`
  EEPROM config to a valid mock-scene node id.

### Protocol

- Documented that real TDMA range payloads must carry `from_id` and `to_id` so
  third-party pair ranges can appear in the control app without becoming local
  anchor distances.
- Documented the SX1280 ranging engine as the required source of node-to-node
  distance measurements; RSSI, SNR, packet timing, and host round trips remain
  diagnostics only.
- Finalized `DEBUG_ENABLE` as radio message type `71` and
  `NODE_QUALITY_REPORT` as radio message type `72`.

### Docs

- Updated architecture, data-flow, data-model, radio protocol, replay, logging,
  ESP32-S3 port, and ranging example docs to separate current `peer_id`
  local-to-peer replay data from the planned endpoint-bearing TDMA contract.
- Documented that the first Phase 2 step is done by one person and cannot be
  parallelized until all four ESP32 boards participate in SX1280 ranging smoke,
  GNSS/NMEA evidence is captured, GPS is disabled through the control app, and
  real trilateration fallback is demonstrated.
- Documented that the current distance-only firmware slice is ranging evidence
  only and does not complete GNSS/trilateration acceptance.
- Documented the ESP32-S3 stale `sdkconfig.*` troubleshooting path for control
  input over USB Serial/JTAG.
- Reconciled `AGENTS.md` with ADR 0002 so on-device SX1280 nav-node integration
  is no longer described as out of repository scope.

### Migration

- No replay fixture migration is required for the distance-only firmware and
  control-channel changes. The future `from_id` / `to_id` implementation must
  update C structs, telemetry codec, replay CSV schema, fixtures, serial JSON,
  control app, and tests in one coordinated change.
- Removed the obsolete root planning checklist after migrating Phase 2 planning
  into PRD and issue-shaped docs.
