const { defineConfig } = require("@playwright/test");
const fs = require("fs");

const chromiumPath = process.env.PLAYWRIGHT_CHROMIUM_EXECUTABLE ||
  (fs.existsSync("/usr/bin/chromium") ? "/usr/bin/chromium" : undefined);

module.exports = defineConfig({
  testDir: "tests/control-app",
  outputDir: "control-app/test-results",
  timeout: 30_000,
  use: {
    baseURL: "http://127.0.0.1:8765",
    viewport: { width: 1280, height: 900 },
    screenshot: "only-on-failure",
    launchOptions: chromiumPath ? { executablePath: chromiumPath } : {},
  },
  webServer: {
    command: "python3 control-app/serve.py --port 8765 --host 127.0.0.1",
    url: "http://127.0.0.1:8765/index.html",
    reuseExistingServer: !process.env.CI,
    timeout: 10_000,
  },
});
