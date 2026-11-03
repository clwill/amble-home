#!/usr/bin/env node
// Dev tool: drive the box's Edit page in a real (headless) Chrome the way a person
// would. Opens a copy of amaran.db, checks that exactly the fixtures the box already
// has are pre-checked, presses Save, and watches the page until it lands on Home or
// gives up. Prints a timestamped transcript of the page's status line.
//
//   node tools/edit-page-flow.mjs http://amble-home.local /path/to/amaran.db [--script web/box/box.js]
//
// --script serves that file in place of the box's /amble/box.js, to replay the flow
// with a page script that is not flashed yet (or the previous one, from git show).
// Needs Google Chrome in /Applications and puppeteer-core (cd web/importer && npm ci).
// Prints names and counts only; keys and MACs never. The Save reboots the box: run it
// against the bench box only.
import fs from "node:fs";
import { createRequire } from "node:module";
const puppeteer = createRequire(new URL("../web/importer/package.json", import.meta.url))("puppeteer-core");

const args = process.argv.slice(2);
const scriptAt = args.indexOf("--script");
const overrideJs = scriptAt >= 0 ? args.splice(scriptAt, 2)[1] : null;
const [rawBase, dbPath] = args;
const base = rawBase?.replace(/\/+$/, "");  // one shape for the landing check and the URLs we build
if (!base || !dbPath) { console.error("usage: edit-page-flow.mjs <http://box> <amaran.db> [--script box.js]"); process.exit(2); }
const t0 = Date.now(); const ts = () => ((Date.now() - t0) / 1000).toFixed(1).padStart(6) + "s";

const browser = await puppeteer.launch({ executablePath: "/Applications/Google Chrome.app/Contents/MacOS/Google Chrome", headless: true });
let ok = false;
try {
  const page = await browser.newPage();
  if (overrideJs) {
    await page.setRequestInterception(true);
    page.on("request", (req) => req.url().endsWith("/amble/box.js")
      ? req.respond({ status: 200, contentType: "text/javascript", body: fs.readFileSync(overrideJs, "utf8") })
      : req.continue());
    console.log("page script from", overrideJs);
  }
  page.on("console", (m) => { if (m.type() === "error") console.log(ts(), "console.error:", m.text()); });
  await page.goto(`${base}/amble/edit`, { waitUntil: "networkidle0", timeout: 30000 });
  await page.waitForFunction(() => document.body.dataset.ready === "1", { timeout: 15000 });
  console.log(ts(), "page ready");
  await (await page.$("#file")).uploadFile(dbPath);
  await page.waitForFunction(() => document.querySelectorAll("#rows tr").length > 0, { timeout: 15000 });
  const rows = await page.$$eval("#rows tr", (trs) => trs.map((tr) => ({ name: tr.children[1].textContent, checked: tr.querySelector("input").checked })));
  const ticked = rows.filter((r) => r.checked).map((r) => r.name);
  console.log(ts(), `rows: ${rows.length}, pre-checked: ${ticked.join(", ") || "(none)"}`);
  console.log(ts(), `fixtures line: ${await page.$eval("#fixtures-msg", (e) => e.textContent)}`);  // names nothing: counts by reason only
  let last = "", landed = false;
  if (ticked.length === 0) console.log("nothing pre-checked: this box has no fixtures yet; check on the real page instead");
  else await page.click("#save");
  for (let i = 0; ticked.length > 0 && i < 150; i++) {
    await new Promise((r) => setTimeout(r, 1000));
    let url, msg;
    try { url = page.url(); msg = url.endsWith("/amble/edit") ? await page.$eval("#msg", (e) => e.textContent) : "(left the Edit page)"; } catch { url = page.url(); msg = "(page in transition)"; }
    const line = `${url} | ${msg}`;
    if (line !== last) { console.log(ts(), line); last = line; }
    if (url === `${base}/`) { landed = true; break; }
    if (/Could not confirm|Save failed/.test(msg)) break;
  }
  if (landed) {
    await page.waitForFunction(() => document.querySelectorAll("#rows tr").length > 0, { timeout: 15000 }).catch(() => {});
    console.log(ts(), "home lists:", await page.$$eval("#rows tr", (trs) => trs.map((tr) => tr.children[1]?.textContent).join(", ")));
  }
  ok = landed;
  console.log(ts(), landed ? "RESULT: landed on Home" : "RESULT: did not reach Home");
} finally { await browser.close(); }
process.exit(ok ? 0 : 1);
