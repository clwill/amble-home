#!/usr/bin/env node
// Assembles the box's web assets into web/box/dist/ (gitignored). The ESPHome
// component embeds every file in dist/ gzipped at codegen; run this before
// `esphome compile`. sql.js comes from web/importer's node_modules.
import { copyFileSync, mkdirSync, statSync, existsSync, readdirSync, readFileSync, writeFileSync, rmSync } from "node:fs";
import { dirname, join, resolve } from "node:path";
import { fileURLToPath } from "node:url";

const here = dirname(fileURLToPath(import.meta.url));
const importer = resolve(here, "../importer");
const sqljs = join(importer, "node_modules/sql.js/dist");
const dist = join(here, "dist");

if (!existsSync(join(sqljs, "sql-wasm.wasm"))) {
  console.error(`sql.js not found at ${sqljs}\nRun: cd web/importer && npm install`);
  process.exit(1);
}
mkdirSync(dist, { recursive: true });

// One visual system, three pages: copy theme.css into each page between its
// theme markers, in place, so every page stays a complete file on its own.
const theme = readFileSync(join(here, "theme.css"), "utf8").trim();
const START = "/* theme:start */", END = "/* theme:end */";
for (const page of [join(here, "home.html"), join(here, "edit.html"), resolve(here, "../flasher/index.html")]) {
  const html = readFileSync(page, "utf8");
  const a = html.indexOf(START), b = html.indexOf(END);
  if (a < 0 || b < a) { console.error(`${page}: theme markers missing`); process.exit(1); }
  const next = html.slice(0, a + START.length) + "\n" + theme + "\n" + html.slice(b);
  if (next !== html) writeFileSync(page, next);
}

const files = [
  [join(here, "home.html"), "home.html"],
  [join(here, "box.js"), "box.js"],
  [join(importer, "importer.js"), "importer.js"],
  [join(importer, "fixture-caps.json"), "fixture-caps.json"],
  [join(sqljs, "sql-wasm.js"), "sql-wasm.js"],
  [join(sqljs, "sql-wasm.wasm"), "sql-wasm.wasm"],
];
for (const [src, name] of files) copyFileSync(src, join(dist, name));

// The cloud door (AMBLE_CLOUD_DOOR: "0" strips it; any other value, or unset, keeps it). edit.html carries
// it in two marker forms: HTML comments in the markup, line comments in the
// module script (an HTML comment is a syntax error there). Off removes each
// region whole; on removes only the marker lines. The source file is untouched.
const cloud = (process.env.AMBLE_CLOUD_DOOR ?? "1") !== "0";
const forms = [
  ["<!-- cloud:start -->", "<!-- cloud:end -->"],
  ["// cloud:start", "// cloud:end"],
];
let edit = readFileSync(join(here, "edit.html"), "utf8");
for (const [open, close] of forms) {
  const re = cloud
    ? new RegExp(`^[ \\t]*(${open}|${close})[ \\t]*\\r?\\n`, "gm")
    : new RegExp(`^[ \\t]*${open}[\\s\\S]*?${close}[ \\t]*\\r?\\n`, "gm");
  edit = edit.replace(re, "");
}
if (/cloud:(start|end)/.test(edit)) { console.error("edit.html: unbalanced cloud markers"); process.exit(1); }
writeFileSync(join(dist, "edit.html"), edit);
if (cloud) copyFileSync(join(importer, "cloud.js"), join(dist, "cloud.js"));
else rmSync(join(dist, "cloud.js"), { force: true });
console.log(`cloud door: ${cloud ? "on" : "off"}`);
for (const f of readdirSync(dist).sort()) console.log(`${String(statSync(join(dist, f)).size).padStart(8)}  ${f}`);
