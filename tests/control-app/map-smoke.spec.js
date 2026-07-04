const { test, expect } = require("@playwright/test");
const path = require("path");

test("offline demo fixture renders GNSS and no-GPS markers", async ({ page }, testInfo) => {
  await page.goto("/index.html?demo=1");

  const map = page.locator("#map");
  await expect(map).toBeVisible();
  await expect(page.locator(".node-marker")).toHaveCount(4);
  await expect(page.locator("#fieldChecklist tr")).toHaveCount(4);
  await expect.poll(() => page.locator("#map").evaluate((el) => Number(el.dataset.rangeLines || "0"))).toBeGreaterThanOrEqual(3);

  await expect(page.locator('.node-marker.gnss .node-label', { hasText: "node-0" })).toBeVisible();
  await expect(page.locator('.node-marker.gnss .node-label', { hasText: "node-2" })).toBeVisible();
  await expect(page.locator('.node-marker.gnss .node-label', { hasText: "node-3" })).toBeVisible();
  await expect(page.locator('.node-marker.radio.this .node-label', { hasText: "node-1 (this, no GPS)" })).toBeVisible();

  const labels = await page.locator(".node-marker .node-label").allTextContents();
  expect(labels.join(" ")).not.toContain("0.00 m");
  await expectMarkersInsideMap(page);
  await expect(page.locator("#fullscreenMap")).toBeVisible();
  await page.locator("#fullscreenMap").click();
  await expect.poll(() => page.evaluate(() => !!(document.fullscreenElement || document.webkitFullscreenElement))).toBe(true);
  await expect(page.locator("#fullscreenMap")).toHaveText("Exit full");
  await expect.poll(async () => page.locator("#map").evaluate((el) => {
    const box = el.getBoundingClientRect();
    return window.innerHeight - box.height;
  })).toBeLessThan(90);
  await page.locator("#fullscreenMap").click();
  await expect.poll(() => page.evaluate(() => !!(document.fullscreenElement || document.webkitFullscreenElement))).toBe(false);
  await expect(page.locator("#fullscreenMap")).toHaveText("Fullscreen");
  await expectMarkersInsideMap(page);

  await page.screenshot({
    path: path.join(testInfo.outputDir, "control-app-map-demo.png"),
    fullPage: true,
  });
});

test("records typed and text telemetry to downloadable NDJSON", async ({ page }) => {
  await page.goto("/index.html?demo=1");

  await page.locator("#recordToggle").click();
  await expect(page.locator("#recordToggle")).toHaveText("Stop (0)");

  await page.evaluate(() => {
    handleLine(JSON.stringify({
      type: "range",
      ts: 2000,
      from_id: 1,
      to_id: 2,
      request_id: 7,
      ok: true,
      range_mm: 5100,
      rssi_dbm: -61,
      snr_db: 8,
      source: "log",
    }));
    handleLine("t=2010ms [INFO] [RANGE] range_result ok=true from=1 to=3 request_id=8 range_mm=4300 rssi_dbm=-63 snr_db=7");
  });

  await expect(page.locator("#recordToggle")).toHaveText("Stop (3)");
  const downloadPromise = page.waitForEvent("download");
  await page.locator("#recordToggle").click();
  const download = await downloadPromise;
  expect(download.suggestedFilename()).toMatch(/^nav-mcu-node-1-.*\.ndjson$/);
  const stream = await download.createReadStream();
  const chunks = [];
  for await (const chunk of stream) chunks.push(chunk);
  const text = Buffer.concat(chunks).toString("utf8");
  const lines = text.trim().split("\n").map((line) => JSON.parse(line));
  expect(lines.map((line) => line.type)).toEqual(["meta", "range", "log", "range"]);
  expect(lines[3]).toMatchObject({ type: "range", from_id: 1, to_id: 3, ok: true, range_mm: 4300 });
});

test("failed scans do not make offline nodes look present", async ({ page }) => {
  await page.goto("/index.html");

  await page.evaluate(() => {
    render({
      t: 1000,
      node: { id: 0, name: "node-0", gps: true, mock: false },
      mode: "GNSS_NAV_OK",
      sol: "LOCAL_GNSS",
      src: "LOCAL_GNSS",
      reject: "NONE",
      pos: { lat_e7: 504520000, lon_e7: 305260000, alt_mm: 0 },
      num_anchors: 0,
      position_source: "GNSS",
      position_valid: true,
      position_degraded: false,
      peers: [1, 2, 3].map((id) => ({
        id,
        gnss: false,
        lat_e7: 0,
        lon_e7: 0,
        alt_mm: 0,
        position_source: "NONE",
        position_valid: false,
        position_degraded: false,
        telemetry_age_ms: 0,
        range_mm: 0,
        range_valid: false,
      })),
    });

    for (const id of [1, 2, 3]) {
      handleLine(`t=1200ms [WARN] [RANGE] range_result ok=false from=0 to=${id} request_id=${id} range_fail_reason=TIMEOUT`);
    }
  });

  await expect(page.locator("#fieldChecklist tr")).toHaveCount(4);
  await expect(page.locator("#fieldChecklist tr").nth(0).locator("td").nth(1)).toHaveText(/fresh/);
  for (const index of [1, 2, 3]) {
    await expect(page.locator("#fieldChecklist tr").nth(index).locator("td").nth(1)).toHaveText("missing");
    await expect(page.locator("#fieldChecklist tr").nth(index).locator("td").nth(3)).toHaveText("fail");
  }
});

async function expectMarkersInsideMap(page) {
  await expect.poll(async () => page.evaluate(() => {
    const mapBox = document.querySelector("#map")?.getBoundingClientRect();
    const markers = Array.from(document.querySelectorAll(".node-marker")).map((marker) => {
      const box = marker.getBoundingClientRect();
      const centerX = box.left + box.width / 2;
      const centerY = box.top + box.height / 2;
      return {
        left: box.left,
        right: box.right,
        top: box.top,
        bottom: box.bottom,
        width: box.width,
        height: box.height,
        centerX,
        centerY,
      };
    });
    if (!mapBox) return { ok: false, reason: "missing map", markers };
    const inside = markers.filter((box) =>
      box.width > 0 &&
      box.height > 0 &&
      box.centerX >= mapBox.left &&
      box.centerX <= mapBox.right &&
      box.centerY >= mapBox.top &&
      box.centerY <= mapBox.bottom
    );
    return {
      ok: inside.length >= 4,
      reason: `${inside.length} of ${markers.length} markers inside map`,
      markers,
    };
  })).toMatchObject({ ok: true });
}
