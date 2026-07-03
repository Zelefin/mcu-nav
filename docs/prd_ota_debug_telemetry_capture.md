# PRD: On-Demand OTA Debug Telemetry + Capture + AI Analysis

## Problem Statement

We want to analyze **network quality** and **trilateration quality** of the real
multi-node system after the fact, and use an AI agent to turn recordings into
findings and recommendations. None of that is possible today:

- The telemetry UI captures durable sessions; the old single-file UI only had a volatile 400-line serial-log view,
  only the latest snapshot, one observation per pair, and no export/storage. Log
  collection is net-new.
- The control-channel snapshot exposes ~10 per-peer fields; the core computes far
  more quality data (residual RMS, geometry, per-peer freshness/quality, reject
  reasons) that is dropped before serialization.
- A connected node can only see its own **network view**. It cannot learn each
  *remote* node's own solution/GNSS quality — the data most needed for
  whole-system trilateration quality.
- On hardware the only over-the-air traffic is SX1280 ranging plus best-effort
  `NRR1` range-report **text**; received reports are logged, never decoded, and no
  node can ask another node to do anything.

The desired outcome: an operator connects to one node, enables **debug telemetry
mode**, and that node asks peers over the radio to broadcast a compact **node
quality report** (best-effort, never preempting ranging, off by default). The
connected node decodes these and emits **typed records** on the control channel;
the telemetry UI streams them to disk as an **NDJSON capture session**; an
`analyze-capture` skill reads the capture and produces a quality report with
recommendations.

This work is **firmware-first**. The browser side is captured as requirements
only, because the UI has moved to React under `telemetry-ui/` and will be built to
this contract later.

See ADR 0004 (transport + lifetime decisions), `docs/radio_protocol.md`
(proposed messages), `docs/capture_ndjson.md` (capture schema), and `CONTEXT.md`
(glossary) for the language and contracts this PRD implements.

## Solution

Deliver, in this order:

1. **Protocol contract + codec (portable core, host-tested).** Add the proposed
   `DEBUG_ENABLE` and `NODE_QUALITY_REPORT` messages/payloads to
   `nav_radio_protocol` and the `nav_telemetry` codec, with round-trip host tests.
2. **Firmware OTA path.** Generalize the best-effort packet path into typed
   message dispatch, add the debug-enable broadcast + auto-timeout, and the
   node-quality-report emit/decode — all best-effort in the radio task's idle
   window so ranging cadence is unchanged.
3. **Control channel.** Add the `debug` command (RAM-only flag) and move
   control-channel output to a typed record envelope
   (`snapshot`/`node_quality`/`range`/`log`).
4. **AI analysis skill.** `analyze-capture` reads an NDJSON capture and emits a
   findings report with prioritized recommendations.
5. **Browser (requirements only).** Record/Debug toggles and File System Access
   streaming of the typed records to an NDJSON file, implemented during the React
   migration.

The compact node quality report combines with existing air-report pair ranges to
approximate a whole-system network + trilateration quality picture from a single
connected node.

## User Stories

1. As an operator, I want to enable debug telemetry mode on the connected node
   from the telemetry UI, so that peers start reporting their quality without a
   firmware reflash.
2. As an operator, I want debug telemetry to be off after every power cycle, so
   that a node never silently steals air time in normal operation.
3. As an operator, I want peers to auto-stop reporting shortly after I disconnect
   or disable debug, so that leaving the field returns the network to normal.
4. As a radio developer, I want the node quality report to travel best-effort in
   idle time, so that ranging master scans and slave listen keep their cadence.
5. As a radio developer, I want a message-type discriminator on best-effort
   packets, so that `NRR1` text reports and typed frames can coexist and be
   dispatched.
6. As a navigation developer, I want each node's own solve quality (residual RMS,
   geometry score, total quality, anchor selection) and GNSS health reported, so
   that whole-system trilateration quality is observable from one node.
7. As a navigation developer, I want received quality reports treated as
   diagnostics with freshness and never folded into any anchor table, so that the
   solver inputs stay clean.
8. As a telemetry-UI user (React), I want a Record toggle that streams every
   control-channel record to a file, so that I can capture a real run for later.
9. As a telemetry-UI user (React), I want a Debug toggle independent of Record, so
   that I can view live or capture with or without debug telemetry.
10. As an analyst, I want an NDJSON capture with a documented schema, so that the
    AI agent and humans can interpret it reproducibly.
11. As an analyst, I want an `analyze-capture` skill that reports network and
    trilateration quality plus prioritized recommendations, so that a capture
    turns into action without manual number-crunching.
12. As a maintainer, I want the protocol enums, payload structs, and
    `docs/radio_protocol.md` kept in sync, and the AGENTS.md radio line reconciled
    with ADR 0002, so that the contract stays trustworthy.

## Implementation Decisions

Design decisions are recorded in ADR 0004 and `CONTEXT.md`. Concrete build notes:

### Protocol + codec (`core/`)

- Add `DEBUG_ENABLE` and `NODE_QUALITY_REPORT` to `nav_radio_protocol.h` with the
  payload layouts in `docs/radio_protocol.md`. Finalize message-type numbers
  (candidates: reuse `GET_STATUS`=6 and `STATS`=69, or add dedicated ids).
- Encode/decode in `nav_telemetry.c` (+ header); map `NODE_QUALITY_REPORT` to a
  core struct the firmware can buffer per peer. Keep fixed-width types and explicit
  units per AGENTS.md; keep the core free of platform APIs.
- `geometry_score`/`total_quality` are 0..1 in core, sent as `_u8` (×255) on the
  wire; residuals sent as `_mm_u16` (clamp/saturate large values).

### Radio task (`src/RadioHealth.cpp`)

- Generalize the best-effort path: add a message-type byte so `serviceReportRx`
  (~:203) dispatches legacy `NRR1` text and typed frames; add a typed-frame TX
  helper beside `broadcastRangeReport` (~:174).
- When local debug telemetry mode is active, periodically broadcast `DEBUG_ENABLE`
  (with TTL) **only in the idle listen window**, never delaying the next master
  ranging turn (`nextMasterAtMs`) or slave listen.
- On `DEBUG_ENABLE` RX, set `debug_active_until_ms = now + ttl`; while active,
  best-effort broadcast this node's `NODE_QUALITY_REPORT` built from
  `nav_core_get_snapshot()`, again only in guard time; auto-revert when the TTL
  lapses.
- On `NODE_QUALITY_REPORT` RX, decode and hand to `ControlChannel` (extend the
  existing `handleEvent` path ~:265/:276) to buffer per peer.
- **Priority guarantee:** every telemetry TX is gated behind "enough guard time
  before the next ranging action." Ranging behavior must be unchanged when debug
  is off, and effectively unchanged when on.

### Control channel (`src/ControlChannel.cpp`, `core/src/nav_serial_json.*`)

- Add a `debug` verb: enum in `nav_serial_json.h` (~:27), parser branch in
  `nav_serial_json.c` (~:201), `applyCommand` case (~:91). It sets a **RAM-only
  atomic flag**; do **not** call `NodeConfigStore::save` (no NVS field). Expose a
  runtime accessor (e.g. `ControlChannel::isDebugEnabled()`), read by the radio
  task, separate from the persisted `getConfig` (~:610).
- Move control-channel output to a typed envelope
  `{"type":"<kind>","ts":<device_ms>,...}`. Kinds: `snapshot` (current
  `nav_serial_write_snapshot` payload), `node_quality` (per-peer + local, from the
  radio buffer), `range` (result incl. `air_report`), `log` (text). Add sibling
  writers beside `nav_serial_write_snapshot` (~:51). `EmitterTask` (~:173) emits
  snapshot + buffered node_quality every 500 ms; range/log as they occur.
- **Legacy break:** the deprecated `control-app/index.html` `render()` expects a bare
  snapshot object and will not parse the typed envelope. Acceptable because the app
  is being replaced by React. An optional bare-snapshot compat line may be kept as
  a temporary shim, not the target design.

### AI analysis skill (`.claude/skills/analyze-capture/`)

- Reads one NDJSON capture (+ `docs/capture_ndjson.md`) and computes network
  quality (per-pair ranging success rate, rssi/snr distributions, range
  stability/outliers via `range_sigma_mm`, staleness/`packet_seq`-gap loss) and,
  when solutions exist, trilateration quality (residual RMS, geometry score, total
  quality trends, reject-reason frequencies, `LOCAL_GNSS` vs `RADIO_3D` mix).
- Correlates anomalies across nodes/links; outputs a markdown findings report with
  prioritized, human-actionable recommendations. No autonomous edits.

### Browser (requirements only — React migration)

- Do not edit deprecated `control-app/index.html`; update `telemetry-ui/`.
- **Debug toggle:** sends `{"cmd":"debug","on":true|false}`; independent of Record.
- **Record toggle:** `showSaveFilePicker()` → `FileSystemWritableFileStream`;
  stream every inbound control-channel record as one NDJSON line, wrapping each
  with a browser `ts_ms`; write the `meta` header first; close on stop; gate on
  connection.
- **Future view:** re-open via `showOpenFilePicker()` or via offline tooling/the
  skill; no in-browser session library.
- Exact UI, file rollover policy, and viewer are clarified at React
  implementation time.

## Testing Decisions

- Host: `cmake -S . -B build && cmake --build build && ctest --test-dir build
  --output-on-failure`. New `DEBUG_ENABLE`/`NODE_QUALITY_REPORT` encode/decode
  round-trip and bounds tests pass in `tests/test_radio_protocol.c` /
  `tests/test_telemetry.c`. Existing replay tests (`ctest -R replay`) unchanged.
- On-device (hardware integration gate, 4 nodes): flash 4 boards; connect the
  telemetry UI (or a serial capture) to one; enable debug telemetry mode; confirm
  (a) peers' `node_quality` records arrive at the connected node, (b) ranging
  master/slave cadence is **not** degraded (priority held), (c) peers auto-revert
  within the TTL after debug is stopped / node disconnects, (d) no node boots in
  debug after power-cycle.
- Capture + skill: pipe the connected node's serial to an NDJSON file (interim, no
  React needed); run `analyze-capture`; confirm a coherent findings report.
- Follow ADR 0003: any change to data models, protocol messages, or solver
  behavior updates logs and docs in the same change.

## Out of Scope

- Full radio framing (COBS/CRC32/ACK) — see README Next-Milestone #4.
- Scheduled TDMA telemetry slots (best-effort chosen instead — ADR 0004).
- Full per-peer link matrix from every node (compact per-node report only for now).
- Config auto-tuning and closed improvement loops (analyze & advise only).
- Deterministic offline converter / replay+plot pipeline reuse; in-browser session
  library; multi-node live merge.
- Adding platform dependencies to the portable core; rewriting trilateration math;
  flight-controller / MAVLink integration.

## Further Notes

- ADR 0002's superseding decision already puts on-device radio and
  `nav_radio_protocol` (as the OTA format) in this repo; the `AGENTS.md`
  "Do not implement SX1280 radio firmware" line is stale and should be reconciled
  when this lands.
- An issue-shaped backlog can live under
  `docs/issues/ota_debug_telemetry_capture/` following the phase-2 pattern, split
  by the five solution steps above.
- The grilling design record for this feature is the approved plan file
  (session artifact); this PRD is the durable, in-repo handoff.
