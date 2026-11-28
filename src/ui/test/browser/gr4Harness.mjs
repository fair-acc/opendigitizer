// GR4's headless-browser harness (devtools/browser of the pinned GR4, node 22 or later), found where CMake says in OD_GR4_BROWSER_HARNESS,
// and the init script that asks the UI for its browser-test hooks (globalThis.odTestHooks, read once at start-up).
// A test importing this module is skipped when the harness is absent.

import { existsSync } from "node:fs";
import path from "node:path";
import { pathToFileURL } from "node:url";

const directory = process.env.OD_GR4_BROWSER_HARNESS ?? "";
if (!existsSync(path.join(directory, "browser_harness.mjs"))) {
  console.log(`skipped: GR4's browser harness not found (OD_GR4_BROWSER_HARNESS='${directory}')`);
  process.exit(77);
}

export const { launchBrowser, openPage, startServer } = await import(pathToFileURL(path.join(directory, "browser_harness.mjs")).href);
export const { decodePng } = await import(pathToFileURL(path.join(directory, "png.mjs")).href);

export const kTestHooks = "globalThis.odTestHooks = true;";
