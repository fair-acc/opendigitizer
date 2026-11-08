// The browser XtraLocalisationStereo dashboard in headless Chrome with its fake microphones: the input list and the pair B
// picker offer the browser's audio inputs as 'label [deviceId]', which BrowserMicrophonePairs reports as available_devices.

import { launchBrowser, openPage, startServer } from "./harness.mjs";
import { existsSync } from "node:fs";
import path from "node:path";

const [webDirectory] = process.argv.slice(2);
if (!webDirectory) {
  console.error("usage: qa_BrowserMicrophones.mjs <ui-wasm web directory>");
  process.exit(2);
}
if (!existsSync(path.join(webDirectory, "index.html"))) {
  console.log(`skipped: no WASM UI build in ${webDirectory}`);
  process.exit(77);
}

const failures = [];
const expect = (condition, description) => {
  console.log(`  ${condition ? "ok  " : "FAIL"}  ${description}`);
  if (!condition) {
    failures.push(description);
  }
};

const server = await startServer(path.resolve(webDirectory));
const browser = await launchBrowser(["--use-fake-device-for-media-stream", "--use-fake-ui-for-media-stream", "--autoplay-policy=no-user-gesture-required"]);
try {
  const page = (globalThis.lastPage = await openPage(browser, `${server.origin}/index.html#dashboard=XtraLocalisationStereo`));
  console.log(`opened ${server.origin}/index.html#dashboard=XtraLocalisationStereo`);
  const input = await page.waitFor(`globalThis.odDevices?.input?.length > 0 && globalThis.odDevices.input`, "the browser's audio inputs in the input list", 60_000);
  expect(input.every((d) => /^.+ \[.+\]$/.test(d)), `listed as 'label [deviceId]' (got ${input.join(", ")})`);
  expect(input.some((d) => d.startsWith("Fake")), "Chrome's fake microphones among them");
  const pairB = await page.waitFor(`globalThis.odDevices?.pairB?.length > 0 && globalThis.odDevices.pairB`, "the same inputs in the pair B picker", 30_000);
  expect(pairB.length === input.length, `pair B offers the same ${input.length} inputs (got ${pairB.length})`);
  page.close();
} catch (error) {
  failures.push(String(error));
  console.error(error);
  console.error(`page console (last 30 lines):\n${(globalThis.lastPage?.consoleLines ?? []).slice(-30).join("\n")}`);
} finally {
  await browser.stop();
  server.stop();
}
console.log(failures.length === 0 ? "all browser microphone checks passed" : `${failures.length} failed`);
process.exit(failures.length === 0 ? 0 : 1);
