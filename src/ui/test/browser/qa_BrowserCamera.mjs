// The XtraCameraDemo dashboard in headless Chrome: the test card at its requested rate through the GR4 stream into an
// ImageChart, and Chrome's fake camera (a test-card Y4M clip) through getUserMedia into another. Frames are identified
// by the test card's barcode, read from screenshots at the chart's on-screen frame position (globalThis.odImageCharts).

import { launchBrowser, openPage, startServer, until } from "./harness.mjs";
import { execFileSync } from "node:child_process";
import { existsSync, mkdtempSync, rmSync } from "node:fs";
import { tmpdir } from "node:os";
import path from "node:path";

const [webDirectory, y4mTool] = process.argv.slice(2);
if (!webDirectory || !y4mTool) {
  console.error("usage: qa_BrowserCamera.mjs <ui-wasm web directory> <ex_test_card_y4m>");
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

// mirrors test_card::Layout and readBarcode() in src/blocks/video/include/opendigitizer/video/TestCard.hpp
const kBarcodeBits = 16;
const readBarcode = (shot, chart) => {
  const cell = Math.max(1, Math.floor(chart.columns / (kBarcodeBits + 4)));
  const strip = Math.max(1, Math.floor(chart.rows / 16));
  const row = chart.rows - strip + strip / 2;
  const toScreen = (column, imageRow) => [Math.round(chart.x + ((column - chart.x0) / (chart.x1 - chart.x0)) * chart.w), Math.round(chart.y + ((imageRow - chart.y0) / (chart.y1 - chart.y0)) * chart.h)];
  const isOn = (c) => {
    const [x, y] = toScreen(c * cell + cell / 2, row);
    const o = (y * shot.width + x) * 4;
    return shot.rgba[o] + shot.rgba[o + 1] + shot.rgba[o + 2] > 3 * 128;
  };
  if (!isOn(1) || !isOn(kBarcodeBits + 2) || isOn(0)) {
    return null;
  }
  let value = 0;
  for (let c = 2; c < kBarcodeBits + 2; ++c) {
    value = value * 2 + (isOn(c) ? 1 : 0);
  }
  return value;
};

const sampleBarcodes = async (page, name, count, timeoutMs = 60_000) => {
  const samples = [];
  const expiry = Date.now() + timeoutMs;
  while (samples.length < count) {
    if (Date.now() > expiry) {
      const file = path.join(process.env.OD_BROWSER_ARTIFACTS ?? mkdtempSync(path.join(tmpdir(), "qa_BrowserCamera-")), `${name.replace(/\W/g, "_")}.png`);
      await page.savePng(file);
      console.error(`screenshot: ${file}, chart: ${JSON.stringify(await page.evaluate(`globalThis.odImageCharts[${JSON.stringify(name)}]`))}`);
      throw new Error(`read only ${samples.length} of ${count} barcodes from '${name}': ${samples.map((s) => s.value).join(" ")}`);
    }
    const shot = await page.screenshot();
    const chart = await page.evaluate(`globalThis.odImageCharts[${JSON.stringify(name)}]`);
    const value = readBarcode(shot, chart);
    if (value !== null && (samples.length === 0 || samples.at(-1).value !== value)) {
      samples.push({ value });
    }
  }
  return samples;
};

// Chrome's fake camera offers no image controls: brightness and zoom stand in for a real camera's
const kMockedControls = `(() => {
  const capabilities = MediaStreamTrack.prototype.getCapabilities, settings = MediaStreamTrack.prototype.getSettings;
  const mocked = { brightness: { min: 0, max: 255, step: 1, value: 128 }, zoom: { min: 100, max: 400, step: 1, value: 100 } };
  MediaStreamTrack.prototype.getCapabilities = function () { return { ...capabilities.call(this), ...Object.fromEntries(Object.entries(mocked).map(([k, { value, ...range }]) => [k, range])) }; };
  MediaStreamTrack.prototype.getSettings = function () { return { ...settings.call(this), ...Object.fromEntries(Object.entries(mocked).map(([k, { value }]) => [k, value])) }; };
})();`;

const scratch = mkdtempSync(path.join(tmpdir(), "od-browser-camera-"));
process.on("exit", () => rmSync(scratch, { recursive: true, force: true }));
const clip = path.join(scratch, "test_card.y4m");
execFileSync(y4mTool, [clip, "640", "480", "25", "100"]);

const server = await startServer(path.resolve(webDirectory));
const browser = await launchBrowser(["--use-fake-device-for-media-stream", "--use-fake-ui-for-media-stream", `--use-file-for-fake-video-capture=${clip}`, "--autoplay-policy=no-user-gesture-required"]);
try {
  const page = (globalThis.lastPage = await openPage(browser, `${server.origin}/index.html#dashboard=XtraCameraDemo`, kMockedControls));
  await page.command("Emulation.setDeviceMetricsOverride", { width: 1280, height: 800, deviceScaleFactor: 1, mobile: false });
  console.log(`opened ${server.origin}/index.html#dashboard=XtraCameraDemo`);
  for (const name of ["Test card", "Camera"]) {
    await page.waitFor(`(globalThis.odImageCharts?.[${JSON.stringify(name)}]?.shown ?? 0) > 0`, `a frame in '${name}'`, 120_000);
    console.log(`'${name}' shows frames`);
  }

  console.log("test card through the GR4 stream into ImageChart, frame by frame");
  const card = await page.evaluate(`globalThis.odImageCharts["Test card"]`);
  expect(card.rows === 240 && card.columns === 320, `320x240 frame (got ${card.columns}x${card.rows})`);
  const cardSamples = await sampleBarcodes(page, "Test card", 12);
  const increasing = cardSamples.every((s, i) => i === 0 || s.value > cardSamples[i - 1].value);
  expect(increasing, `barcodes increase: ${cardSamples.map((s) => s.value).join(" ")}`);
  const after = await page.evaluate(`globalThis.odImageCharts["Test card"]`);
  const advanced = cardSamples.at(-1).value - cardSamples[0].value;
  expect(after.skipped === 0 && after.displayed - card.displayed >= advanced, `every test-card frame shown, none skipped (${after.displayed - card.displayed} shown while the barcode advanced ${advanced}, ${after.skipped} skipped)`);

  console.log("browser camera: Chrome's fake device playing the test-card clip");
  const camera = await page.evaluate(`globalThis.odImageCharts["Camera"]`);
  expect(camera.rows === 480 && camera.columns === 640, `640x480 frame (got ${camera.columns}x${camera.rows})`);
  const cameraSamples = await sampleBarcodes(page, "Camera", 4);
  expect(cameraSamples.every((s) => s.value < 100), `clip frame numbers read from the screen: ${cameraSamples.map((s) => s.value).join(" ")}`);

  console.log("browser camera controls reach the toolbar control");
  const controls = await page.waitFor(`globalThis.odCameraControls?.cameraControl?.length > 0 && globalThis.odCameraControls.cameraControl`, "the camera's controls in the toolbar control", 30_000);
  expect(controls.includes("brightness") && controls.includes("zoom"), `mocked brightness and zoom offered (got ${controls.join(", ")})`);
  const devices = await page.waitFor(`!(globalThis.odCameraDevices?.cameraControl ?? ["Browser camera"]).some((d) => d.startsWith("Browser camera")) && globalThis.odCameraDevices.cameraControl`, "the browser's own camera names", 30_000);
  expect(devices.some((d) => d.includes("[browser:")), `the browser's cameras listed by name (got ${devices.join(", ")})`);
  page.close();
} catch (error) {
  failures.push(String(error));
  console.error(error);
  console.error(`page console (last 30 lines):\n${(globalThis.lastPage?.consoleLines ?? []).slice(-30).join("\n")}`);
} finally {
  await browser.stop();
  server.stop();
}
console.log(failures.length === 0 ? "all browser camera checks passed" : `${failures.length} failed`);
process.exit(failures.length === 0 ? 0 : 1);
