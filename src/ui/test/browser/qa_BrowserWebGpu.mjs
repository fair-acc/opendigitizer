// The WebGPU build of the WASM UI runs a block on the browser's GPU: WebGpuDemo's FFT, on compute_domain 'gpu', dispatches kernels to the adapter (SwiftShader's gpu:swift here) rather than falling back to the CPU.

import { kTestHooks, launchBrowser, openPage, startServer } from "./gr4Harness.mjs";
import { existsSync } from "node:fs";
import path from "node:path";

const [webDirectory] = process.argv.slice(2);
if (!webDirectory) {
  console.error("usage: qa_BrowserWebGpu.mjs <WebGPU WASM UI web directory>");
  process.exit(2);
}
if (!existsSync(path.join(webDirectory, "index.html")) || !existsSync(path.join(webDirectory, "index.kernels.json"))) {
  console.log(`skipped: no WebGPU build of the WASM UI (index.kernels.json) in ${webDirectory}`);
  process.exit(77);
}

const server = await startServer([path.resolve(webDirectory)]);
const browser = await launchBrowser(); // GR4's harness enables navigator.gpu (SwiftShader, or the hardware adapter with GR4_BROWSER_GPU=hardware)
let failure = null;
try {
  const page = await openPage(browser, `${server.origin}/index.html#dashboard=WebGpuDemo`, { initScript: kTestHooks });
  try {
    const loaded = await page.waitFor("globalThis.odDashboard?.current", { what: "the load result of WebGpuDemo", timeoutMs: 120_000 });
    if (loaded.loaded !== true) {
      throw new Error(`WebGpuDemo did not load: ${loaded.error}`);
    }
    const served = await page.waitFor("globalThis.odWebGpu?.state", { what: "the WebGPU runtime's registration", timeoutMs: 120_000 });
    if (served.served !== "gpu:swift" && served.served !== "gpu:webgpu") {
      throw new Error(`no WebGPU domain is served: ${JSON.stringify(served)}`);
    }
    const ran = await page.waitFor("globalThis.odWebGpu?.state?.dispatches > 0 && globalThis.odWebGpu.state", { what: "a kernel dispatched to the adapter", timeoutMs: 120_000 });
    console.log(`ok: ${ran.served} served, ${ran.dispatches} kernels ran on the adapter`);
  } catch (error) {
    failure = error;
    console.error(`page console (last 30 lines):\n${page.consoleLines.slice(-30).join("\n")}`);
  } finally {
    await page.close();
  }
} finally {
  await browser.stop();
  server.stop();
}
if (failure) {
  console.error(`FAIL: ${failure.message}`);
  process.exit(1);
}
