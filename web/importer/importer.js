// amble-home importer: the amaran.db import logic, shared by the box's Edit page
// (browser, sql.js served by the box) and the Node tools/tests. Ported from the
// studio bridge's scripts/generate-config.ts; proven on a real database in Spike 3. By default keys are never returned, only
// their presence and shape; `withKeys: true` (the box-side payload, later) is the
// one explicit exception. Database errors never surface raw: SQLite messages can
// echo the SQL or data that caused them, so they are replaced at this boundary.

export const FALLBACK_CAPS = {
  // Newer fixtures absent from older fixture_config.json builds.
  "401C5": { color: false, gm: false, cct_min: 2500, cct_max: 7500 },
};
export const DEFAULT_CAPS = { color: false, gm: true, cct_min: 2700, cct_max: 6500 };

// Capability lookup: fixture_config.json keys are `fixture_<code>_<hwver>`.
// Exact code+hw match first, else the lowest hw version for the code.
export function capsFromConfig(cfg, code, hwver) {
  if (!cfg || !code) return null;
  let key;
  const exact = hwver ? `fixture_${code}_${hwver}` : undefined;
  if (exact && exact in cfg) {
    key = exact;
  } else {
    const cand = Object.keys(cfg).filter((k) => k.startsWith(`fixture_${code}_`)).sort();
    if (!cand.length) return null;
    key = cand[0];
  }
  const e = cfg[key];
  const s = (k, d = "0") => String(e[k] ?? d);
  const color = s("hsi_support") === "1" || s("rgb_support") === "1";
  const gm = s("gm_support") === "1";
  let cct_min = Math.trunc(parseFloat(s("product_cct_min", "0"))) * 100;
  let cct_max = Math.trunc(parseFloat(s("product_cct_max", "0"))) * 100;
  if (!(cct_min > 0) || !(cct_max > 0) || cct_min >= cct_max) {
    cct_min = 2700;
    cct_max = 6500;
  }
  return { color, gm, cct_min, cct_max, src: key };
}

export function resolveCaps(cfg, code, hwver) {
  const fromCfg = capsFromConfig(cfg, code, hwver);
  if (fromCfg) return { ...fromCfg, source: "fixture_config" };
  if (code in FALLBACK_CAPS) return { ...FALLBACK_CAPS[code], source: "fallback table" };
  return { ...DEFAULT_CAPS, source: "default (code not in config)" };
}

export const HEX32 = /^[0-9a-f]{32}$/;

function rows(db, sql) {
  const stmt = db.prepare(sql);
  const out = [];
  while (stmt.step()) out.push(stmt.getAsObject());
  stmt.free();
  return out;
}

function hasColumn(db, table, column) {
  return rows(db, `PRAGMA table_info(${table})`).some((r) => r.name === column);
}

// Everything the product importer needs from an open sql.js Database, except the
// key values themselves. `withKeys: true` is for the box-side payload later; the
// spike never sets it.
export class ImportError extends Error {}

// A fixture row is usable only with a well-formed MAC: it becomes the entity id
// on the box and an HTML attribute on the Edit page, so nothing else may pass.
export const MAC_RE = /^([0-9A-F]{2}:){5}[0-9A-F]{2}$/;

// The name as the box will hold it: the firmware keeps 31 bytes (fixture.cpp
// NAME_LEN = 32 with the terminator). Cut here on a codepoint boundary so the page
// compares like with like and never shows a half character.
export function boxName(name) {
  const enc = new TextEncoder();
  let out = "", used = 0;
  for (const ch of String(name)) {
    const n = enc.encode(ch).length;
    if (used + n > 31) break;
    out += ch; used += n;
  }
  return out;
}

// Two records with one MAC (a fixture re-provisioned and left twice), two MACs
// sharing their last three bytes (one entity id, see fixture.h) or two fixtures on
// one mesh address cannot both reach the box: it refuses the whole import and the
// page would have no way to pick one (#34, #36).
// Same MAC, then same address among the MAC survivors: keep the newest by
// `newness(f)` (ties: the later record), the record amaran itself treats as current.
// Same id, different MACs: both are real fixtures, so the first record stays and the
// later one is reported. Known edge: a fixture re-provisioned onto an address a newer
// fixture also holds loses both its records (the new one by address, the old one by
// MAC); both are reported, and the amaran app is where it gets fixed.
const idKey = (mac) => mac.slice(9);
export function dedupe(fixtures, newness) {
  const byNewest = fixtures.map((f, i) => ({ f, i })).sort((a, b) => (newness(b.f) - newness(a.f)) || b.i - a.i);
  const macs = new Set(), keep = new Set(), dropped = [];
  for (const { f, i } of byNewest) {
    if (macs.has(f.mac)) dropped.push({ f, i, reason: "duplicate MAC" });
    else { macs.add(f.mac); keep.add(i); }
  }
  const addrs = new Set();
  for (const { f, i } of byNewest) {
    if (!keep.has(i)) continue;
    if (addrs.has(f.address)) { keep.delete(i); dropped.push({ f, i, reason: "duplicate address" }); }
    else addrs.add(f.address);
  }
  const ids = new Set();
  fixtures.forEach((f, i) => {
    if (!keep.has(i)) return;
    if (ids.has(idKey(f.mac))) { keep.delete(i); dropped.push({ f, i, reason: "id clash" }); }
    else ids.add(idKey(f.mac));
  });
  dropped.sort((a, b) => a.i - b.i);
  return { kept: fixtures.filter((_, i) => keep.has(i)), dropped: dropped.map(({ f, reason }) => ({ f, reason })) };
}
const age = (v) => Number(v) || 0;

export function extract(db, capsCfg, opts = {}) {
  try {
    return extractUnsafe(db, capsCfg, opts);
  } catch (e) {
    if (e instanceof ImportError) throw e;
    // Deliberately drop the cause: a raw SQLite message may contain key material.
    throw new ImportError("This file could not be read as an amaran database (details withheld).");
  }
}

function extractUnsafe(db, capsCfg, { withKeys = false } = {}) {
  const schema = rows(db, "SELECT version_num FROM alembic_version")[0]?.version_num ?? null;

  const mesh = rows(db, "SELECT net_key, app_key FROM mesh LIMIT 1")[0];
  if (!mesh) throw new ImportError("This database has no mesh network (empty mesh table).");
  const netKey = String(mesh.net_key ?? "").trim().toLowerCase();
  const appKey = String(mesh.app_key ?? "").trim().toLowerCase();
  const keys = {
    net_key_present: HEX32.test(netKey),
    app_key_present: HEX32.test(appKey),
    // The bridge's generator tolerated an iv_index column that this schema does
    // not have; the firmware recovers the IV index from the secure network beacon.
    iv_index: hasColumn(db, "mesh", "iv_index")
      ? parseInt(String(rows(db, "SELECT iv_index FROM mesh LIMIT 1")[0]?.iv_index ?? "0"), 10) || 0
      : null,
  };
  if (withKeys) Object.assign(keys, { net_key: netKey, app_key: appKey });

  const updateTime = hasColumn(db, "fixtures", "update_time") ? "update_time" : "NULL AS update_time";
  const fixtureRows = rows(
    db,
    `SELECT mac_address, node_address, name, code, control_hw_version, device_tag, state, ${updateTime} ` +
      "FROM fixtures WHERE node_address > 1 ORDER BY node_address",
  );
  const fixtures = [];
  const skipped = [];
  for (const f of fixtureRows) {
    const mac = String(f.mac_address ?? "").trim().toUpperCase();
    const addr = parseInt(String(f.node_address), 10);
    const name = boxName(String(f.name ?? "").trim() || `Light ${addr}`);
    const code = String(f.code ?? "").trim().toUpperCase();
    const hwver = String(f.control_hw_version ?? "").trim();
    if (!MAC_RE.test(mac) || !(addr >= 2)) {
      skipped.push({ name, reason: !MAC_RE.test(mac) ? "bad MAC" : "bad address" });
      continue;
    }
    const caps = resolveCaps(capsCfg, code, hwver);
    fixtures.push({ mac, address: addr, name, code, hwver, device_tag: f.device_tag, caps, _t: age(f.update_time) });
  }
  const { kept, dropped } = dedupe(fixtures, (f) => f._t);
  for (const { f, reason } of dropped) skipped.push({ name: f.name, reason });
  for (const f of kept) delete f._t;

  const groups = rows(db, "SELECT name, node_address, type FROM groups ORDER BY node_address").map((g) => ({
    name: g.name,
    address: parseInt(String(g.node_address), 10),
    type: g.type,
  }));

  return { schema, keys, fixtures: kept, skipped, groups };
}

export const hex4 = (n) => "0x" + n.toString(16).toUpperCase().padStart(4, "0");

// The same-origin POST body the box's /amble/import endpoint accepts (plan task 8).
// `selected` is a list of MACs to include; undefined means every fixture. Keys are
// included because this payload is destined for the box, never for a log.
export function toImportPayload(result, { selected } = {}) {
  if (!result?.keys?.net_key || !result?.keys?.app_key) {
    throw new ImportError("This import has no keys to send (extract it with withKeys).");
  }
  const want = selected ? new Set(selected.map((m) => String(m).toUpperCase())) : null;
  const fixtures = result.fixtures
    .filter((f) => !want || want.has(f.mac))
    .map((f) => ({
      mac: f.mac,
      address: f.address,
      name: f.name,
      color: !!f.caps.color,
      gm: !!f.caps.gm,
      cct_min: f.caps.cct_min,
      cct_max: f.caps.cct_max,
    }));
  if (!fixtures.length) throw new ImportError("No fixtures selected.");
  // The box's own mesh address is never part of the payload: it derives it from its MAC (#25).
  return { version: 1, net_key: result.keys.net_key, app_key: result.keys.app_key, fixtures };
}
