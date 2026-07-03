import { readFileSync } from "node:fs";
import { describe, expect, it } from "vitest";
import { parseInboundLine, validateCaptureLines } from "./controlRecords";

const sampleCapture = readFileSync(new URL("../../fixtures/sample_capture.ndjson", import.meta.url), "utf8");

describe("control record parsing", () => {
  it("validates the shared sample capture", () => {
    const result = validateCaptureLines(sampleCapture);

    expect(result.ok).toBe(true);
    expect(result.errors).toEqual([]);
    expect(result.records[0]).toMatchObject({
      type: "meta",
      schema_version: 1,
      node_id: 2,
      debug: true,
    });
    expect(result.records.filter((record) => record.type === "node_quality")).toHaveLength(3);
    expect(result.records.filter((record) => record.type === "range")).toHaveLength(3);
  });

  it("wraps a legacy bare snapshot as a typed snapshot record", () => {
    const parsed = parseInboundLine(
      JSON.stringify({
        t: 42,
        node: { id: 2, name: "alpha", gps: false, mock: true },
        peers: [],
      }),
      1000,
    );

    expect(parsed?.source).toBe("legacy-snapshot");
    expect(parsed?.record).toMatchObject({
      type: "snapshot",
      ts_ms: 1000,
      ts: 42,
      data: { node: { id: 2 }, peers: [] },
    });
  });

  it("keeps legacy range logs visible and extracts a range observation", () => {
    const parsed = parseInboundLine(
      't=45080ms [WARN] [RANGE] range_result ok=false from=1 to=2 request_id=13 range_fail_reason=TIMEOUT source=air_report rssi_dbm=-54.0 snr_db=13.0',
      2000,
    );

    expect(parsed?.record).toMatchObject({
      type: "log",
      ts_ms: 2000,
      ts: 45080,
      level: "WARN",
      tag: "RANGE",
    });
    expect(parsed?.rangeFromText).toMatchObject({
      type: "range",
      ts_ms: 2000,
      ts: 45080,
      from_id: 1,
      to_id: 2,
      request_id: 13,
      ok: false,
      range_fail_reason: "TIMEOUT",
      source: "air_report",
    });
  });
});
