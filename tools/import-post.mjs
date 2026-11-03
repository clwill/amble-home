#!/usr/bin/env node
// Dev tool: build the import payload from an amaran.db copy and POST it to a box,
// then follow the import to its result.
//   node tools/import-post.mjs <amaran.db> <host> [--select MAC[,MAC...]] [--pin 1234] [--rename MAC=name]
// Prints only HTTP statuses, the import state and the fixture count. Keys, MACs
// and names never print. --pin sends the box's Edit PIN; --rename changes one
// fixture's name in the payload (bench tool for the rename-stability check, #14).
import fs from "node:fs";
import initSqlJs from "../web/importer/node_modules/sql.js/dist/sql-wasm.js";
import { extract, toImportPayload, ImportError } from "../web/importer/importer.js";

const [dbPath, host, ...rest] = process.argv.slice(2);
if (!dbPath || !host) { console.error("usage: import-post.mjs <amaran.db> <host> [--select MAC,MAC] [--pin 1234] [--rename MAC=name]"); process.exit(2); }
const opt = (k) => { const i = rest.indexOf(k); return i >= 0 ? rest[i + 1] : undefined; };
const selected = opt("--select")?.split(",");
const pin = opt("--pin");
const rename = opt("--rename")?.split(/=(.*)/s);

const caps = JSON.parse(fs.readFileSync(new URL("../web/importer/fixture-caps.json", import.meta.url), "utf-8"));
let payload;
try {
  const SQL = await initSqlJs();
  const db = new SQL.Database(new Uint8Array(fs.readFileSync(dbPath)));
  payload = toImportPayload(extract(db, caps, { withKeys: true }), { selected });
  db.close();
  if (rename) {
    const f = payload.fixtures.find((x) => x.mac === rename[0].toUpperCase());
    if (!f) { console.error("--rename: that MAC is not in the payload"); process.exit(1); }
    f.name = rename[1];
  }
} catch (e) {
  console.error("import failed:", e instanceof ImportError ? e.message : "could not read the database (details withheld)");
  process.exit(1);
}
const res = await fetch(`http://${host}/amble/import`, { method: "POST", headers: { "content-type": "application/json", ...(pin ? { "X-Amble-Pin": pin } : {}) }, body: JSON.stringify(payload) });
const text = await res.text();
console.log(`POST /amble/import -> HTTP ${res.status}: ${text.slice(0, 160)} (${payload.fixtures.length} fixture(s) sent)`);
if (!res.ok) process.exit(1);

// Follow the import: queued -> committed (box reboots) | failed.
const sleep = (ms) => new Promise((r) => setTimeout(r, ms));
let seenCommitted = false;
for (let i = 0; i < 40; i++) {
  await sleep(500);
  let st;
  try {
    const r = await fetch(`http://${host}/amble/status`, { signal: AbortSignal.timeout(3000) });
    st = (await r.json()).import;
  } catch {
    if (seenCommitted) { console.log("box went down for its reboot: import complete"); process.exit(0); }
    continue;
  }
  if (st?.state === "committed") { if (!seenCommitted) console.log("import committed to NVS; the box reboots to build the entities"); seenCommitted = true; }
  if (st?.state === "failed") { console.error("import failed on the box:", st.error ?? "(no detail)"); process.exit(1); }
}
console.log(seenCommitted ? "import committed (reboot not observed yet)" : "import still queued after 20 s; check the box log");
process.exit(seenCommitted ? 0 : 1);
