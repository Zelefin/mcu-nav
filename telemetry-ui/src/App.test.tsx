// @vitest-environment jsdom

import { cleanup, fireEvent, render, screen, waitFor, within } from "@testing-library/react";
import { afterEach, beforeEach, describe, expect, it, vi } from "vitest";

const twoNodeCaptureWithExtraAnchors = [
  {
    type: "meta",
    ts_ms: 1751539200000,
    schema_version: 1,
    app_version: "react-0.1.0",
    firmware_build: "test",
    node_id: 1,
    node_name: "node-mcu",
    debug: true,
  },
  {
    type: "snapshot",
    ts_ms: 1751539200500,
    ts: 100,
    data: {
      t: 100,
      node: { id: 1, name: "node-mcu", gps: false, mock: false },
      mode: "NO_NAV_SOLUTION",
      sol: "NONE",
      src: "NONE",
      reject: "NOT_ENOUGH_ANCHORS",
      pos: { lat_e7: 0, lon_e7: 0, alt_mm: 0 },
      num_anchors: 0,
      peers: [
        { id: 1, gnss: false, lat_e7: 0, lon_e7: 0, alt_mm: 0, range_valid: false },
        { id: 2, gnss: false, lat_e7: 0, lon_e7: 0, alt_mm: 0, range_valid: false },
      ],
    },
  },
  {
    type: "node_quality",
    ts_ms: 1751539200800,
    ts: 120,
    node_id: 1,
    origin: "local",
    age_ms: 0,
    data: {
      nav_mode: "NO_NAV_SOLUTION",
      solution_status: "REJECTED",
      solution_source: "NONE",
      residual_rms_m: 0,
      max_residual_m: 0,
      geometry_score: 0,
      total_quality: 0,
      num_anchors: 3,
      anchor_ids: [0, 2, 3],
      fix_type: "NONE",
      satellites: 0,
      hdop_centi: 0,
      hacc_mm: 0,
      vacc_mm: 0,
      lat_e7: 0,
      lon_e7: 0,
      alt_mm: 0,
      packet_seq: 1,
    },
  },
]
  .map((record) => JSON.stringify(record))
  .join("\n");

vi.mock("../fixtures/sample_capture.ndjson?raw", () => ({
  default: twoNodeCaptureWithExtraAnchors,
}));

describe("App network discovery", () => {
  beforeEach(() => {
    localStorage.clear();
  });

  afterEach(() => {
    cleanup();
    vi.clearAllMocks();
  });

  it("does not create distance rows for node_quality anchor_ids that were not received as nodes", async () => {
    const { App } = await import("./App");
    render(<App />);

    fireEvent.click(screen.getByRole("button", { name: /sample/i }));

    const distanceTable = await screen.findByRole("table", { name: /distance observations/i });

    await waitFor(() => {
      expect(within(distanceTable).queryByText(/node-0/)).toBeNull();
      expect(within(distanceTable).queryByText(/node-3/)).toBeNull();
      expect(within(distanceTable).queryByText(/node-mcu #1 <-> node-mcu #1/)).toBeNull();
      expect(within(distanceTable).getByText(/node-mcu #1 <-> node-2 #2/)).toBeTruthy();
    });
  });
});
