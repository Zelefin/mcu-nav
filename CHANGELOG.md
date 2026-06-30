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

### Changed

- Actualized documentation around the ESP32/ESP32-S3 navigation-node focus,
  on-device SX1280 ownership, and secondary SpeedyBee ranging reference.

### Protocol

- Documented that real TDMA range payloads must carry `from_id` and `to_id` so
  third-party pair ranges can appear in the control app without becoming local
  anchor distances.
- Documented the SX1280 ranging engine as the required source of node-to-node
  distance measurements; RSSI, SNR, packet timing, and host round trips remain
  diagnostics only.

### Docs

- Updated architecture, data-flow, data-model, radio protocol, replay, logging,
  ESP32-S3 port, and ranging example docs to separate current `peer_id`
  local-to-peer replay data from the planned endpoint-bearing TDMA contract.

### Migration

- No code or replay fixture migration in this documentation-only change. The
  future `from_id` / `to_id` implementation must update C structs, telemetry
  codec, replay CSV schema, fixtures, serial JSON, control app, and tests in one
  coordinated change.
