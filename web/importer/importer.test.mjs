// node --test. Builds a synthetic amaran.db in memory with the real schema's
// columns; no user data anywhere in this file.
import { test } from "node:test";
import assert from "node:assert/strict";
import fs from "node:fs";
import initSqlJs from "sql.js";
import { extract, resolveCaps, toImportPayload, ImportError, boxName } from "./importer.js";

const caps = JSON.parse(fs.readFileSync(new URL("./fixture-caps.json", import.meta.url), "utf-8"));
const NET = "00112233445566778899aabbccddeeff";
const APP = "ffeeddccbbaa99887766554433221100";
const HEX32 = /[0-9a-fA-F]{32}/;

async function db(rows, { meshRow = `('${NET}','${APP}')`, extraSql = "" } = {}) {
  const SQL = await initSqlJs();
  const d = new SQL.Database();
  d.run(`CREATE TABLE alembic_version (version_num VARCHAR(32) NOT NULL);
         INSERT INTO alembic_version VALUES ('a62d9e4b8f10');
         CREATE TABLE mesh (uuid TEXT, net_key VARCHAR(32), app_key VARCHAR(32), fixtures_ordered_list TEXT, scenes_ordered_list TEXT, update_time INT, state INT);
         INSERT INTO mesh (net_key, app_key) VALUES ${meshRow};
         CREATE TABLE fixtures (uuid TEXT, mac_address TEXT, code TEXT, device_key TEXT, device_uuid TEXT, composition_data TEXT, fast_provision_supported INT, control_hw_version TEXT, name TEXT, node_address INT, update_time INT, state INT, device_tag TEXT DEFAULT 'fixture');
         CREATE TABLE groups (uuid TEXT, scene_uuid TEXT, type INT, name TEXT, node_address INT, update_time INT, state INT);`);
  for (const r of rows) d.run("INSERT INTO fixtures (mac_address, code, device_key, control_hw_version, name, node_address) VALUES (?,?,?,?,?,?)", r);
  if (extraSql) d.run(extraSql);
  return d;
}
const F1 = ["AA:BB:CC:00:00:01", "40015", "deadbeefdeadbeefdeadbeefdeadbeef", "1.0", "Light A", 30];
const F2 = ["AA:BB:CC:00:00:02", "40025", "cafebabecafebabecafebabecafebabe", "1.2", "Light B", 16];
const APPNODE = ["AA:BB:CC:00:00:00", "00000", "", "1.0", "the app", 1];

test("extract reports key presence and never the values", async () => {
  const r = extract(await db([F1]), caps);
  assert.equal(r.keys.net_key_present, true);
  assert.equal(r.keys.app_key_present, true);
  assert.equal(r.keys.iv_index, null);
  assert.ok(!HEX32.test(JSON.stringify(r)), "no 32-hex token in the default result");
});

test("extract filters node_address <= 1 and orders by address", async () => {
  const r = extract(await db([F1, APPNODE, F2]), caps);
  assert.deepEqual(r.fixtures.map((f) => f.address), [16, 30]);
  assert.equal(r.fixtures[0].name, "Light B");
});

test("extract never selects device keys", async () => {
  const r = extract(await db([F1]), caps, { withKeys: true });
  assert.ok(!JSON.stringify(r).includes("deadbeef"), "device_key column must not be read");
});

test("resolveCaps: exact hw match, lowest-hw fallback, fallback table, default", () => {
  assert.equal(resolveCaps(caps, "40015", "1.0").source, "fixture_config");
  assert.equal(resolveCaps(caps, "40025", "9.9").src, "fixture_40025_1.0");
  // The fallback table only matters when the shipped config lacks the code; use an empty config.
  assert.deepEqual(resolveCaps({}, "401C5", "1.0"), { color: false, gm: false, cct_min: 2500, cct_max: 7500, source: "fallback table" });
  assert.equal(resolveCaps(caps, "ZZZZZ", "1.0").source, "default (code not in config)");
  assert.equal(resolveCaps(caps, "ZZZZZ", "1.0").cct_min, 2700);
});

test("extract wraps sqlite errors as ImportError without the cause", async () => {
  const d = await db([F1], { extraSql: `DROP TABLE fixtures; CREATE VIEW fixtures AS SELECT json_extract('{}', '${NET}') AS mac_address, 5 AS node_address, '' AS name, '' AS code, '' AS control_hw_version, '' AS device_tag, 0 AS state;` });
  assert.throws(() => extract(d, caps), (e) => e instanceof ImportError && !e.message.includes(NET));
});

test("extract reports an empty mesh table as a fixed ImportError", async () => {
  const d = await db([F1]); d.run("DELETE FROM mesh");
  assert.throws(() => extract(d, caps), (e) => e instanceof ImportError && /no mesh network/.test(e.message));
});

test("toImportPayload includes keys only from a withKeys extract and respects selection", async () => {
  const d = await db([F1, F2]);
  const r = extract(d, caps, { withKeys: true });
  const all = toImportPayload(r);
  assert.equal(all.version, 1);
  assert.equal(all.net_key, NET);
  assert.equal(all.fixtures.length, 2);
  assert.deepEqual(Object.keys(all.fixtures[0]).sort(), ["address", "cct_max", "cct_min", "color", "gm", "mac", "name"]);
  const one = toImportPayload(r, { selected: ["aa:bb:cc:00:00:01"] });
  assert.equal(one.fixtures.length, 1);
  assert.equal(one.fixtures[0].address, 30);
  assert.ok(!("own_address" in one), "the box derives its own address; the payload never carries one");
});

test("toImportPayload rejects a result without keys or with nothing selected", async () => {
  const d = await db([F1]);
  assert.throws(() => toImportPayload(extract(d, caps)), ImportError);
  assert.throws(() => toImportPayload(extract(d, caps, { withKeys: true }), { selected: ["00:00:00:00:00:00"] }), ImportError);
});

test("extract skips a fixture whose MAC is malformed (it would become an id and an attribute)", async () => {
  const BAD = ['"><img src=x onerror=alert(1)>', "40015", "", "1.0", "Evil", 40];
  const r = extract(await db([F1, BAD]), caps);
  assert.equal(r.fixtures.length, 1);
  assert.deepEqual(r.skipped, [{ name: "Evil", reason: "bad MAC" }]);
  const SHORT = ["AA:BB:CC:00:01", "40015", "", "1.0", "Short", 41];
  assert.equal(extract(await db([SHORT]), caps).skipped[0].reason, "bad MAC");
});

test("boxName truncates to 31 UTF-8 bytes on a codepoint boundary", () => {
  const bytes = (s) => new TextEncoder().encode(s).length;
  assert.equal(boxName("amaran P60c"), "amaran P60c");
  assert.equal(boxName("a".repeat(31)), "a".repeat(31));
  assert.equal(boxName("a".repeat(32)), "a".repeat(31));
  const cjk = boxName("灯".repeat(11));
  assert.equal(cjk, "灯".repeat(10));
  assert.ok(bytes(cjk) <= 31);
  assert.ok(!boxName("灯".repeat(11) + "x").includes("�"));
  assert.ok(bytes(boxName("😀".repeat(9))) <= 31);
});

// Two records with one MAC, or two MACs with one id (last three bytes), cannot both go to the box.
const F1_AGAIN = ["AA:BB:CC:00:00:01", "40015", "deadbeefdeadbeefdeadbeefdeadbeef", "1.0", "Light A again", 31];
const F1_CLASH = ["11:22:33:00:00:01", "40025", "cafebabecafebabecafebabecafebabe", "1.2", "Clash", 40];

test("extract keeps the newest of two records sharing a MAC and skips the other as duplicate MAC", async () => {
  const r = extract(await db([F1, F1_AGAIN], { extraSql: "UPDATE fixtures SET update_time = 5 WHERE node_address = 30; UPDATE fixtures SET update_time = 9 WHERE node_address = 31;" }), caps);
  assert.deepEqual(r.fixtures.map((f) => f.name), ["Light A again"]);
  assert.deepEqual(r.skipped, [{ name: "Light A", reason: "duplicate MAC" }]);
  assert.ok(!("_t" in r.fixtures[0]), "no bookkeeping field leaks into the result");
});

test("extract: with no update_time the later record wins a duplicate MAC", async () => {
  const r = extract(await db([F1_AGAIN, F1]), caps);  // query order is by address: 30 then 31
  assert.deepEqual(r.fixtures.map((f) => f.address), [31]);
  assert.deepEqual(r.skipped, [{ name: "Light A", reason: "duplicate MAC" }]);
});

test("extract skips the second of two different MACs that would share an entity id", async () => {
  const r = extract(await db([F1, F2, F1_CLASH]), caps);
  assert.deepEqual(r.fixtures.map((f) => f.address), [16, 30]);
  assert.deepEqual(r.skipped, [{ name: "Clash", reason: "id clash" }]);
});

test("extract: newest by update_time wins even when it comes first in address order", async () => {
  const r = extract(await db([F1, F1_AGAIN], { extraSql: "UPDATE fixtures SET update_time = 9 WHERE node_address = 30; UPDATE fixtures SET update_time = 5 WHERE node_address = 31;" }), caps);
  assert.deepEqual(r.fixtures.map((f) => f.name), ["Light A"]);
  assert.deepEqual(r.skipped, [{ name: "Light A again", reason: "duplicate MAC" }]);
});

// Two different fixtures with one mesh address cannot both go to the box either.
const F1_SAME_ADDR = ["AA:BB:CC:00:00:07", "40015", "deadbeefdeadbeefdeadbeefdeadbeef", "1.0", "Light A moved", 30];

test("extract keeps the newest of two records sharing a mesh address and skips the other as duplicate address", async () => {
  const r = extract(await db([F1, F1_SAME_ADDR], { extraSql: "UPDATE fixtures SET update_time = 9 WHERE mac_address = 'AA:BB:CC:00:00:07'; UPDATE fixtures SET update_time = 5 WHERE mac_address = 'AA:BB:CC:00:00:01';" }), caps);
  assert.deepEqual(r.fixtures.map((f) => f.name), ["Light A moved"]);
  assert.deepEqual(r.skipped, [{ name: "Light A", reason: "duplicate address" }]);
});

test("extract: with no update_time the later record wins a duplicate address", async () => {
  const r = extract(await db([F1, F1_SAME_ADDR]), caps);
  assert.deepEqual(r.fixtures.map((f) => f.name), ["Light A moved"]);
  assert.deepEqual(r.skipped, [{ name: "Light A", reason: "duplicate address" }]);
});

// Pins the order of the passes: with the address pass first, "Light A" would be reported as a duplicate address.
test("extract: a duplicate MAC is settled before a duplicate address, and the reasons are kept apart", async () => {
  const r = extract(await db([F1, F1_AGAIN, F1_SAME_ADDR], { extraSql: "UPDATE fixtures SET update_time = 9 WHERE node_address = 31;" }), caps);
  assert.deepEqual(r.fixtures.map((f) => f.name), ["Light A moved", "Light A again"]);
  assert.deepEqual(r.skipped, [{ name: "Light A", reason: "duplicate MAC" }]);
});

test("extract: the duplicate-MAC survivor can still lose an id clash to an earlier record", async () => {
  const C = ["11:22:33:00:00:01", "40025", "cafebabecafebabecafebabecafebabe", "1.2", "Earlier", 20];
  const r = extract(await db([C, F1, F1_AGAIN], { extraSql: "UPDATE fixtures SET update_time = 9 WHERE node_address = 31;" }), caps);
  assert.deepEqual(r.fixtures.map((f) => f.name), ["Earlier"]);
  assert.deepEqual(r.skipped, [{ name: "Light A", reason: "duplicate MAC" }, { name: "Light A again", reason: "id clash" }]);
});
