const { test, expect } = require("@playwright/test");
const path = require("path");

test("offline demo fixture renders GNSS and no-GPS markers", async ({ page }, testInfo) => {
  await page.goto("/index.html?demo=1");

  const map = page.locator("#map");
  await expect(map).toBeVisible();
  await expect(page.locator(".node-marker")).toHaveCount(4);

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
  await page.locator("#fullscreenMap").click();
  await expect.poll(() => page.evaluate(() => !!(document.fullscreenElement || document.webkitFullscreenElement))).toBe(false);
  await expect(page.locator("#fullscreenMap")).toHaveText("Fullscreen");
  await expectMarkersInsideMap(page);

  await page.screenshot({
    path: path.join(testInfo.outputDir, "control-app-map-demo.png"),
    fullPage: true,
  });
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
