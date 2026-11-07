// Drives headless Chrome over the DevTools protocol against the WASM UI (after gr4-present's devtools/browser_harness.mjs).

import { spawn } from "node:child_process";
import { createReadStream, readdirSync } from "node:fs";
import { mkdtemp, readFile, rm, writeFile } from "node:fs/promises";
import { createServer } from "node:http";
import { tmpdir } from "node:os";
import path from "node:path";
import { inflateSync } from "node:zlib";

const kBrowsers = ["google-chrome", "chromium", "chromium-browser", "google-chrome-stable"];
const kTypes = { ".html": "text/html", ".js": "text/javascript", ".mjs": "text/javascript", ".wasm": "application/wasm", ".data": "application/octet-stream", ".json": "application/json" };

export const until = async (probe, what, timeoutMs = 60_000) => {
  const expiry = Date.now() + timeoutMs;
  for (;;) {
    try {
      const value = await probe();
      if (value) {
        return value;
      }
    } catch {
      // not there yet
    }
    if (Date.now() > expiry) {
      throw new Error(`timed out waiting for ${what}`);
    }
    await new Promise((resolve) => setTimeout(resolve, 50));
  }
};

/// static files with the cross-origin isolation headers the threaded WASM runtime needs
export const startServer = (served) =>
  new Promise((resolve) => {
    const directory = path.resolve(served);
    const files     = new Map(readdirSync(directory, { recursive: true, withFileTypes: true }).filter((entry) => entry.isFile()).map((entry) => {
      const file = path.join(entry.parentPath, entry.name);
      return ["/" + path.relative(directory, file).split(path.sep).join("/"), file];
    }));
    const server = createServer((request, response) => {
      let pathname;
      try {
        pathname = decodeURIComponent(new URL(request.url, "http://localhost").pathname);
      } catch {
        response.writeHead(400).end();
        return;
      }
      const file = files.get(pathname); // only the files listed at start: no request path reaches the file system
      if (file === undefined) {
        response.writeHead(404).end();
        return;
      }
      response.writeHead(200, { "Content-Type": kTypes[path.extname(file)] ?? "application/octet-stream", "Cross-Origin-Opener-Policy": "same-origin", "Cross-Origin-Embedder-Policy": "require-corp" });
      createReadStream(file).pipe(response);
    });
    server.listen(0, "127.0.0.1", () => resolve({ origin: `http://localhost:${server.address().port}`, stop: () => server.close() }));
  });

const kContainerArguments = process.env.CI || process.getuid?.() === 0 ? ["--no-sandbox", "--disable-dev-shm-usage"] : []; // CI containers: no user namespaces for Chrome's sandbox, a small /dev/shm

/// the debugging port is the browser's own choice (port 0), read from DevToolsActivePort in its profile
export const launchBrowser = async (browserArguments = []) => {
  for (const candidate of kBrowsers) {
    const profile = await mkdtemp(path.join(tmpdir(), "od-browser-"));
    const removeProfile = () => rm(profile, { recursive: true, force: true, maxRetries: 10, retryDelay: 100 });
    const browser = spawn(candidate, ["--headless=new", "--use-gl=angle", "--use-angle=swiftshader", "--enable-unsafe-swiftshader", "--no-first-run", "--no-default-browser-check", "--window-size=1280,800", `--user-data-dir=${profile}`, "--remote-debugging-port=0", ...kContainerArguments, ...browserArguments, "about:blank"], { stdio: ["ignore", "ignore", "pipe"] });
    let stderrTail = "";
    browser.stderr.on("data", (chunk) => (stderrTail = (stderrTail + chunk).slice(-2000)));
    const exited = new Promise((resolve) => browser.once("exit", resolve));
    if (!(await new Promise((resolve) => (browser.once("error", () => resolve(false)), browser.once("spawn", () => resolve(true)))))) {
      await removeProfile();
      continue;
    }
    const stop = async () => {
      if (browser.exitCode === null && browser.signalCode === null) {
        browser.kill("SIGKILL");
      }
      await exited;
      await removeProfile();
    };
    try {
      const port = await until(async () => {
        if (browser.exitCode !== null || browser.signalCode !== null) {
          return `exited: ${browser.exitCode ?? browser.signalCode}`;
        }
        const firstLine = (await readFile(path.join(profile, "DevToolsActivePort"), "utf8")).split("\n")[0];
        return /^[0-9]+$/.test(firstLine) ? firstLine : null;
      }, "the browser's DevToolsActivePort");
      if (!/^[0-9]+$/.test(port)) {
        throw new Error(`${candidate} ${port} before opening its debugging port\n${stderrTail}`);
      }
      const endpoint = `http://127.0.0.1:${port}`;
      await until(() => fetch(`${endpoint}/json/version`).then((r) => r.ok), "the browser's debugging endpoint");
      return { endpoint, stop };
    } catch (error) {
      await stop();
      throw error;
    }
  }
  throw new Error(`no headless browser found, tried: ${kBrowsers.join(", ")}`);
};

/// `initScript` runs in the page before any of its own scripts, e.g. to stand in for a browser API; the UI's test hooks are enabled
export const openPage = async (browser, url, initScript = "") => {
  const target = await (await fetch(`${browser.endpoint}/json/new?about:blank`, { method: "PUT" })).json();
  const socket = new WebSocket(target.webSocketDebuggerUrl);
  const pending = new Map();
  const consoleLines = [];
  let nextId = 0;
  socket.onmessage = (event) => {
    const message = JSON.parse(event.data);
    if (message.id && pending.has(message.id)) {
      pending.get(message.id).resolve(message);
      pending.delete(message.id);
    } else if (message.method === "Runtime.consoleAPICalled") {
      consoleLines.push(message.params.args.map((a) => a.value ?? a.description ?? "").join(" "));
    }
  };
  socket.onclose = () => {
    for (const { reject } of pending.values()) {
      reject(new Error("the browser's debugging connection closed"));
    }
    pending.clear();
  };
  await new Promise((resolve, reject) => ((socket.onopen = resolve), (socket.onerror = reject)));
  const command = (method, params = {}) =>
    new Promise((resolve, reject) => {
      if (socket.readyState !== WebSocket.OPEN) {
        reject(new Error(`${method}: the browser's debugging connection is closed`));
        return;
      }
      pending.set(++nextId, { resolve, reject });
      socket.send(JSON.stringify({ id: nextId, method, params }));
    });
  const evaluate = async (expression) => (await command("Runtime.evaluate", { expression, returnByValue: true })).result?.result?.value;
  await command("Runtime.enable");
  await command("Page.enable");
  await command("Page.addScriptToEvaluateOnNewDocument", { source: `globalThis.odTestHooks = true;\n${initScript}` });
  await command("Page.navigate", { url });
  return {
    command,
    evaluate,
    consoleLines,
    waitFor: (expression, what = expression, timeoutMs) => until(() => evaluate(expression), what, timeoutMs),
    screenshot: async () => decodePng(Buffer.from((await command("Page.captureScreenshot", { format: "png" })).result.data, "base64")),
    savePng: async (file) => writeFile(file, Buffer.from((await command("Page.captureScreenshot", { format: "png" })).result.data, "base64")),
    close: () => socket.close(),
  };
};

const paeth = (a, b, c) => {
  const p = a + b - c;
  const [pa, pb, pc] = [Math.abs(p - a), Math.abs(p - b), Math.abs(p - c)];
  return pa <= pb && pa <= pc ? a : pb <= pc ? b : c;
};

/// 8-bit non-interlaced RGB(A) as Chrome emits → { width, height, rgba }
export const decodePng = (buffer) => {
  let width = 0, height = 0, channels = 0;
  const parts = [];
  for (let offset = 8; offset < buffer.length; ) {
    const length = buffer.readUInt32BE(offset);
    const type = buffer.toString("ascii", offset + 4, offset + 8);
    const data = buffer.subarray(offset + 8, offset + 8 + length);
    offset += 12 + length;
    if (type === "IHDR") {
      [width, height, channels] = [data.readUInt32BE(0), data.readUInt32BE(4), data.readUInt8(9) === 6 ? 4 : 3];
    } else if (type === "IDAT") {
      parts.push(data);
    }
  }
  const raw = inflateSync(Buffer.concat(parts));
  const stride = width * channels;
  const rgba = Buffer.alloc(width * height * 4);
  let previous = Buffer.alloc(stride);
  for (let row = 0; row < height; ++row) {
    const filter = raw[row * (stride + 1)];
    const current = Buffer.from(raw.subarray(row * (stride + 1) + 1, (row + 1) * (stride + 1)));
    for (let i = 0; i < stride; ++i) {
      const left = i >= channels ? current[i - channels] : 0;
      const upperLeft = i >= channels ? previous[i - channels] : 0;
      current[i] = (current[i] + [0, left, previous[i], (left + previous[i]) >> 1, paeth(left, previous[i], upperLeft)][filter]) & 0xff;
    }
    for (let column = 0; column < width; ++column) {
      for (let c = 0; c < 4; ++c) {
        rgba[(row * width + column) * 4 + c] = c < channels ? current[column * channels + c] : 0xff;
      }
    }
    previous = current;
  }
  return { width, height, rgba };
};
