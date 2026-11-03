// Node harness for the importer logic against a real amaran.db copy.
// Standing rule 2: nothing derived from the user's database is printed — no
// names, addresses, MACs, model codes or keys. Only fixed diagnostics: counts,
// key presence, capability-source tallies and structural checks, then PASS/FAIL.
// The fixture table itself is for the browser page (importer.html), not the log.
//   node test.mjs [path/to/amaran.db]
import fs from "node:fs";
import initSqlJs from "sql.js";
import { extract, ImportError } from "../../web/importer/importer.js";

const dbPath = process.argv[2] ?? new URL("./amaran.db", import.meta.url).pathname;
const caps = JSON.parse(fs.readFileSync(new URL("../../web/importer/fixture-caps.json", import.meta.url), "utf-8"));

let r;
try {
  const SQL = await initSqlJs();
  const db = new SQL.Database(new Uint8Array(fs.readFileSync(dbPath)));
  r = extract(db, caps);
  db.close();
} catch (e) {
  console.error("FAIL:", e instanceof ImportError ? e.message : "could not open the database (details withheld)");
  process.exit(1);
}

const checks = [];
const check = (name, ok) => checks.push({ name, ok });
check("schema version present", typeof r.schema === "string" && r.schema.length > 0);
check("net_key present (32 hex)", r.keys.net_key_present);
check("app_key present (32 hex)", r.keys.app_key_present);
check("at least one fixture", r.fixtures.length >= 1);
check("every fixture address is unicast (0x0002-0x7FFF)", r.fixtures.every((f) => f.address >= 2 && f.address < 0x8000));
check("every fixture has a 17-char MAC", r.fixtures.every((f) => /^([0-9A-F]{2}:){5}[0-9A-F]{2}$/.test(f.mac)));
check("every fixture has a non-empty name", r.fixtures.every((f) => f.name.length > 0));
check("fixture addresses are unique", new Set(r.fixtures.map((f) => f.address)).size === r.fixtures.length);
check("every fixture has a CCT range", r.fixtures.every((f) => f.caps.cct_min > 0 && f.caps.cct_min < f.caps.cct_max));
check("no group address collides with a fixture", !r.groups.some((g) => r.fixtures.some((f) => f.address === g.address)));

const sources = {};
for (const f of r.fixtures) sources[f.caps.source] = (sources[f.caps.source] ?? 0) + 1;

const lines = [
  `fixtures: ${r.fixtures.length}, skipped: ${r.skipped.length}, groups: ${r.groups.length}`,
  `iv_index column: ${r.keys.iv_index === null ? "absent" : "present"}`,
  `colour-capable: ${r.fixtures.filter((f) => f.caps.color).length}, green/magenta: ${r.fixtures.filter((f) => f.caps.gm).length}`,
  `capability sources: ${Object.entries(sources).map(([k, v]) => `${k}=${v}`).join(", ")}`,
  ...checks.map((c) => `${c.ok ? "ok  " : "FAIL"} ${c.name}`),
];
// Belt and braces: refuse to print if a 32-hex token (either case) slipped in.
if (lines.some((l) => /[0-9a-fA-F]{32}/.test(l))) {
  console.error("FAIL: a 32-hex token is present in the output; refusing to print");
  process.exit(2);
}
console.log(lines.join("\n"));
const failed = checks.filter((c) => !c.ok).length;
console.log(failed ? `FAIL (${failed} check(s))` : "PASS");
process.exit(failed ? 1 : 0);
