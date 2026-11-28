// Every built-in sample dashboard loads in the WASM UI: all its blocks exist in the browser build's registry (the
// native registry can hold names the browser build lacks). The UI publishes each load result as odDashboard.current.

import { kTestHooks, launchBrowser, openPage, startServer } from "./gr4Harness.mjs";
import { existsSync, readdirSync } from "node:fs";
import path from "node:path";
import { fileURLToPath } from "node:url";

const kNativeOnly = new Set(["PulsedPowerDemo", "XtraLocalisationActive"]); // PicoScope hardware; drives a loudspeaker

const [webDirectory] = process.argv.slice(2);
if (!webDirectory) {
  console.error("usage: qa_BrowserSampleDashboards.mjs <ui-wasm web directory>");
  process.exit(2);
}
if (!existsSync(path.join(webDirectory, "index.html"))) {
  console.log(`skipped: no WASM UI build in ${webDirectory}`);
  process.exit(77);
}

const sampleDirectory = path.resolve(path.dirname(fileURLToPath(import.meta.url)), "../../assets/sampleDashboards");
const dashboards = readdirSync(sampleDirectory)
  .filter((file) => file.endsWith(".grc"))
  .map((file) => file.slice(0, -".grc".length))
  .filter((name) => !kNativeOnly.has(name))
  .sort();

const failures = [];
const server = await startServer([path.resolve(webDirectory)]);
const browser = await launchBrowser(["--use-fake-device-for-media-stream", "--use-fake-ui-for-media-stream", "--autoplay-policy=no-user-gesture-required"]);
try {
  for (const name of dashboards) {
    const page = await openPage(browser, `${server.origin}/index.html#dashboard=${name}`, { initScript: kTestHooks });
    try {
      const result = await page.waitFor(`globalThis.odDashboard?.current`, { what: `the load result of ${name}`, timeoutMs: 120_000 });
      const loaded = result.loaded === true;
      console.log(`  ${loaded ? "ok  " : "FAIL"}  ${name}${loaded ? "" : `: ${result.error}`}`);
      if (!loaded) {
        failures.push(`${name}: ${result.error}`);
      }
    } catch (error) {
      failures.push(`${name}: ${error}`);
      console.error(`page console of ${name} (last 20 lines):\n${page.consoleLines.slice(-20).join("\n")}`);
    } finally {
      await page.close();
    }
  }
} finally {
  await browser.stop();
  server.stop();
}

if (failures.length > 0) {
  console.error(`${failures.length} of ${dashboards.length} sample dashboards fail to load:\n  ${failures.join("\n  ")}`);
  process.exit(1);
}
console.log(`all ${dashboards.length} sample dashboards load`);
