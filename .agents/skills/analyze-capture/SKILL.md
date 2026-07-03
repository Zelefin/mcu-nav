---
name: analyze-capture
description: Analyze a control-app NDJSON capture session for network and trilateration quality, and produce a findings report with prioritized recommendations. Use when the user points at a captured .ndjson file (or asks to analyze a capture / debug-telemetry recording) from the nav-mcu control app.
---

# Analyze Capture

Turn one **capture session** (an NDJSON recording of a nav-mcu control-channel
session) into a quality report: what the network and trilateration are doing,
where they are weak, and what to try next. **Analyze and advise only — never edit
firmware, config, or the capture.**

## Inputs

- A path to a `.ndjson` capture file (the argument). If none is given, ask for it.
- The schema: read `docs/capture_ndjson.md` first — it defines every record type
  and field. Read `CONTEXT.md` for the domain language (debug telemetry mode, node
  quality report, network view). Treat `node_quality` and `air_report` ranges as
  **diagnostics with freshness**, never anchor inputs.

## Procedure

1. **Load & validate.** Read the file line by line. Confirm the first line is a
   `meta` record; capture `schema_version`, `node_id`, `firmware_build`, `debug`.
   Skip malformed lines but count them. Note the wall-clock span from `ts_ms`.
   For large files, prefer a small script (jq / Python) over reading the whole
   file into context; report the commands you ran so the numbers are reproducible.

2. **Inventory.** Count records per `type`; list node ids seen (connected node +
   any `node_quality`/`range` endpoints). State whether debug telemetry mode was
   active (presence of `node_quality` with `origin:"peer"`). If there are no
   peer `node_quality` records, say so — the whole-system view is limited to the
   connected node's own reports and air-report ranges.

3. **Network quality** (from `range` records, per unordered pair `from_id↔to_id`):
   - Ranging **success rate** = ok / (ok + fail); list worst pairs first.
   - `rssi_dbm` / `snr_db` distribution per pair (min/median/max); flag links with
     median RSSI below ~-85 dBm or low SNR.
   - Range **stability**: spread of `range_mm` and typical `range_sigma_mm` per
     pair; flag high-sigma or bimodal pairs (multipath/geometry suspects).
   - `range_fail_reason` frequencies (TIMEOUT vs RANGING_ENGINE_ERROR vs …).
   - Apparent **packet loss** / staleness: gaps in `packet_seq` across a node's
     `node_quality` records; long stretches with no records from a node.

4. **Trilateration quality** (from `node_quality` + `snapshot`, per node that
   solves):
   - Trends of `residual_rms_m`, `max_residual_m`, `geometry_score`,
     `total_quality`; flag sustained high residuals or low geometry.
   - `num_anchors` and `anchor_ids` churn (unstable anchor selection).
   - `reject_reason` / snapshot `reject` frequencies (e.g. NOT_ENOUGH_ANCHORS,
     STALE_RANGE, BAD_GEOMETRY).
   - Solution-source mix (`LOCAL_GNSS` vs `RADIO_3D` vs NONE) and mode
     transitions over time.
   - GNSS health per node: `fix_type`, `satellites`, `hdop_centi`, `hacc/vacc`.
   - If a capture has **no** `RADIO_3D` solutions (a distance-only run), say so and
     focus the report on network quality.

5. **Correlate.** Tie network weaknesses to navigation outcomes — e.g. "pair
   1↔3 fails 40% and drops as an anchor exactly when node 2 reports
   geometry_score < 0.4 and reject=BAD_GEOMETRY." Prefer a few well-supported
   correlations over many weak ones.

6. **Report.** Write a markdown findings report:
   - **Summary** — 3-5 bullets: overall network health, trilateration health, the
     single biggest problem.
   - **Findings** — ranked most-severe first; each with the evidence (numbers,
     pairs/nodes, time ranges) it rests on.
   - **Recommendations** — prioritized, human-actionable (antenna/placement,
     spacing/geometry, which link/node to inspect, candidate `nav_config`
     thresholds to revisit — as *suggestions*, not applied changes).
   - **Data caveats** — capture span, dropped lines, missing peer reports,
     distance-only vs full-nav.

## Rules

- Reproducible numbers: show the queries/commands behind every statistic.
- No autonomous edits (config auto-tuning / closed loop is explicitly out of
  scope — see `docs/prd_ota_debug_telemetry_capture.md`).
- Be honest about uncertainty and small samples; do not over-interpret a short or
  sparse capture.
