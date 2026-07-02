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

  await page.screenshot({
    path: path.join(testInfo.outputDir, "control-app-map-demo.png"),
    fullPage: true,
  });
});

async function expectMarkersInsideMap(page) {
  const result = await page.evaluate(() => {
    const mapBox = document.querySelector("#map")?.getBoundingClientRect();
    const markers = Array.from(document.querySelectorAll(".node-marker")).map((marker) => {
      const box = marker.getBoundingClientRect();
      return {
        left: box.left,
        right: box.right,
        top: box.top,
        bottom: box.bottom,
        width: box.width,
        height: box.height,
      };
    });
    if (!mapBox) return { ok: false, reason: "missing map", markers };
    const inside = markers.filter((box) =>
      box.width > 0 &&
      box.height > 0 &&
      box.right >= mapBox.left &&
      box.left <= mapBox.right &&
      box.bottom >= mapBox.top &&
      box.top <= mapBox.bottom
    );
    return {
      ok: inside.length === 4,
      reason: `${inside.length} markers inside map`,
      markers,
    };
  });
  expect(result, result.reason).toMatchObject({ ok: true });
}
