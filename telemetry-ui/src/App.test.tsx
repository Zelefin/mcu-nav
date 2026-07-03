// @vitest-environment jsdom

import { cleanup, fireEvent, render, screen, waitFor, within } from "@testing-library/react";
import { afterEach, describe, expect, it, vi } from "vitest";

const twoNodeCaptureWithExtraAnchors = [
  {
    type: "meta",
    ts_ms: 1751539200000,
    schema_version: 1,
    app_version: "react-0.1.0",
    firmware_build: "test",
    node_id: 2,
    node_name: "speedybee",
    debug: true,
  },
  {
    type: "snapshot",
    ts_ms: 1751539200500,
    ts: 100,
    data: {
      t: 100,
      node: { id: 2, name: "speedybee", gps: false, mock: false },
      mode: "NO_NAV_SOLUTION",
      sol: "NONE",
      src: "NONE",
      reject: "NOT_ENOUGH_ANCHORS",
      pos: { lat_e7: 0, lon_e7: 0, alt_mm: 0 },
      num_anchors: 0,
      peers: [
        { id: 2, gnss: false, lat_e7: 0, lon_e7: 0, alt_mm: 0, range_valid: false },
        { id: 1, gnss: false, lat_e7: 0, lon_e7: 0, alt_mm: 0, range_valid: false, rssi: -61, snr: 9 },
      ],
    },
  },
  {
    type: "node_quality",
    ts_ms: 1751539200800,
    ts: 120,
    node_id: 2,
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
      anchor_ids: [0, 1, 3],
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

const currentNodeWithPlaceholderPeerSlots = [
  {
    type: "meta",
    ts_ms: 1751539300000,
    schema_version: 1,
    app_version: "react-0.1.0",
    firmware_build: "test",
    node_id: 1,
    node_name: "",
    debug: false,
  },
  {
    type: "snapshot",
    ts_ms: 1751539300500,
    ts: 200,
    data: {
      t: 200,
      node: { id: 1, name: "", gps: false, mock: false },
      mode: "NO_NAV_SOLUTION",
      sol: "NONE",
      src: "NONE",
      reject: "NOT_ENOUGH_ANCHORS",
      pos: { lat_e7: 0, lon_e7: 0, alt_mm: 0 },
      num_anchors: 0,
      peers: [
        { id: 0, gnss: false, lat_e7: 0, lon_e7: 0, alt_mm: 0, range_mm: 0, range_valid: false, rssi: 0, snr: 0, quality: 0 },
        { id: 2, gnss: false, lat_e7: 0, lon_e7: 0, alt_mm: 0, range_mm: 0, range_valid: false, rssi: 0, snr: 0, quality: 0 },
        { id: 3, gnss: false, lat_e7: 0, lon_e7: 0, alt_mm: 0, range_mm: 0, range_valid: false, rssi: 0, snr: 0, quality: 0 },
      ],
    },
  },
]
  .map((record) => JSON.stringify(record))
  .join("\n");

describe("App network view", () => {
  afterEach(() => {
    cleanup();
    vi.clearAllMocks();
    Reflect.deleteProperty(window, "showOpenFilePicker");
  });

  it("hides mock/sample controls, removes distance observations, and orders the current node first", async () => {
    Object.defineProperty(window, "showOpenFilePicker", {
      configurable: true,
      value: vi.fn().mockResolvedValue([
        {
          getFile: () =>
            Promise.resolve({
              name: "capture.ndjson",
              text: () => Promise.resolve(twoNodeCaptureWithExtraAnchors),
            }),
        },
      ]),
    });

    const { App } = await import("./App");
    render(<App />);

      expect(screen.queryByRole("button", { name: /sample/i })).toBeNull();
      fireEvent.click(screen.getByRole("button", { name: /open/i }));

    const peerTable = await screen.findByRole("table", { name: /peer snapshot/i });

    await waitFor(() => {
      expect(screen.queryByRole("table", { name: /distance observations/i })).toBeNull();
      expect(screen.getByText("GPS use")).toBeTruthy();
      expect(screen.getByRole("button", { name: /allow gps use/i })).toBeTruthy();
      expect(screen.queryByRole("button", { name: /mock/i })).toBeNull();
      expect(screen.queryByText(/mock peers/i)).toBeNull();
      expect(screen.queryByText(/node names/i)).toBeNull();
      expect(within(peerTable).queryByText(/node-0/)).toBeNull();
      expect(within(peerTable).queryByText(/node-3/)).toBeNull();
      const rows = within(peerTable).getAllByRole("row");
      expect(rows[1].textContent).toContain("speedybee #2 (this)");
      expect(rows[2].textContent).toContain("node-1 #1");
    });
  });

  it("does not render placeholder peer slots as discovered nodes", async () => {
    Object.defineProperty(window, "showOpenFilePicker", {
      configurable: true,
      value: vi.fn().mockResolvedValue([
        {
          getFile: () =>
            Promise.resolve({
              name: "placeholder-slots.ndjson",
              text: () => Promise.resolve(currentNodeWithPlaceholderPeerSlots),
            }),
        },
      ]),
    });

    const { App } = await import("./App");
    render(<App />);

    fireEvent.click(screen.getByRole("button", { name: /open/i }));

    const peerTable = await screen.findByRole("table", { name: /peer snapshot/i });

    await waitFor(() => {
      expect(within(peerTable).getByText(/node-1 #1 \(this\)/)).toBeTruthy();
      expect(within(peerTable).queryByText(/node-0 #0/)).toBeNull();
      expect(within(peerTable).queryByText(/node-2 #2/)).toBeNull();
      expect(within(peerTable).queryByText(/node-3 #3/)).toBeNull();
      expect(within(peerTable).getAllByRole("row")).toHaveLength(2);
    });
  });
});
