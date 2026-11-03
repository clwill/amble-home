import { resolveCaps, MAC_RE, HEX32, boxName, dedupe } from "./importer.js";

// amble-home cloud door: sign in to amaran's cloud through the box, fetch the mesh from the browser
// and map it to the same object the database importer returns. Nothing is stored by the page; the box keeps the sign-in only when the person checks Remember. Nothing is logged: no
// console output of credentials, tokens or server bodies, and userMessage never
// echoes the server's own text (it can be in Chinese and is not ours to show).

export const REGIONS = [
  { id: "us", area: "US", label: "United States" },
  { id: "eu", area: "EU", label: "Europe" },
  { id: "cn", area: "CN", label: "China" },
];

export const QUERY_CURSORS = [
  "mesh", "fixture", "scene", "group", "quickshot", "preset",
  "scene_detail", "group_detail", "quickshot_detail", "camera", "camera_preset",
];

export class CloudError extends Error {
  // kind: "box" | "credentials" | "token" | "network" | "shape" | "nomesh"
  constructor(kind, detail, code) {
    super(`cloud ${kind}`);
    this.name = "CloudError";
    this.kind = kind;
    if (code !== undefined) this.code = code;
    if (detail !== undefined) this.detail = detail;
  }
}

function host(regionId) {
  if (!REGIONS.some((r) => r.id === regionId)) throw new RangeError("unknown region");
  return `https://${regionId}.sidus.link`;
}

export function queryUrl(regionId) {
  const cursors = QUERY_CURSORS.map((c) => `${c}_update_time=0`).join("&");
  return `${host(regionId)}/amaran/pack/query?${cursors}&attr_tags=fixture,camera,microphone`;
}

async function callCloud(url, init, fetchImpl, failKind) {
  let res;
  try {
    res = await fetchImpl(url, init);
  } catch {
    throw new CloudError("network");
  }
  if (!res.ok) throw new CloudError("network", `status ${res.status}`);
  let body;
  try {
    body = await res.json();
  } catch {
    throw new CloudError("shape", "json");
  }
  if (!body || typeof body !== "object") throw new CloudError("shape", "json");
  if (!Number.isInteger(body.code)) throw new CloudError("shape", "code");
  if (body.code !== 20000) {
    throw new CloudError(failKind, typeof body.msg === "string" ? body.msg : undefined, body.code);
  }
  return body;
}

const BOX_LOGIN = "/amble/cloud/login";
const BOX_FORGET = "/amble/cloud/forget";

// One call to the box. Returns the parsed JSON body; maps HTTP refusals to the
// "box" kind (401 is the PIN, or "locked") and anything unreachable or unparsable
// to "network". With remainingMs the call is bounded in real time: an abort
// signal, plus a timer race for a fetchImpl that ignores signals.
async function callBox(url, init, fetchImpl, remainingMs) {
  const bounded = remainingMs !== undefined;
  const ms = bounded ? Math.max(1, remainingMs) : 0;
  let timer;
  const work = (async () => {
    const sig = bounded && typeof AbortSignal !== "undefined" && typeof AbortSignal.timeout === "function" ? AbortSignal.timeout(ms) : undefined;
    const res = await fetchImpl(url, sig ? { ...init, signal: sig } : init);
    let body;
    try { body = await res.json(); } catch { body = undefined; }
    if (res.status === 401) throw new CloudError("box", body && body.error === "locked" ? "locked" : "pin");
    if (!body || typeof body !== "object") throw new CloudError("network");
    if (!res.ok) throw new CloudError("box", typeof body.error === "string" ? body.error : `status ${res.status}`);
    return body;
  })();
  try {
    if (!bounded) return await work;
    const clock = new Promise((_, rej) => { timer = setTimeout(() => rej(new CloudError("network", "timeout")), ms); });
    return await Promise.race([work, clock]);
  } catch (e) {
    if (e instanceof CloudError) throw e;
    if (e && (e.name === "TimeoutError" || e.name === "AbortError")) throw new CloudError("network", "timeout");
    throw new CloudError("network");
  } finally {
    clearTimeout(timer);
    work.catch(() => {});
  }
}

const jsonHeaders = (headers) => ({ ...headers, "Content-Type": "application/json" });

// Sign in through the box (amaran's login refuses a browser's headers). The box
// takes the credentials once, runs the sign-in, and hands the token back once.
export async function loginViaBox(regionId, email, password, {
  remember = false, saved = false, fetchImpl = globalThis.fetch, headers = {},
  sleep = (ms) => new Promise((r) => setTimeout(r, ms)), timeoutMs = 40000, now = Date.now,
} = {}) {
  const payload = saved
    ? { saved: true }
    : { email: typeof email === "string" ? email.trim() : "", password, region: regionId, remember: !!remember };
  const start = now();
  const left = () => timeoutMs - (now() - start);
  const queuedReply = await callBox(BOX_LOGIN, { method: "POST", headers: jsonHeaders(headers), body: JSON.stringify(payload) }, fetchImpl, left());
  if (queuedReply.ok !== true || queuedReply.state !== "queued") throw new CloudError("shape", "box");
  while (left() > 0) {
    await sleep(500);
    const r = await callBox(BOX_LOGIN, { method: "GET", headers: { ...headers } }, fetchImpl, left());
    if (typeof r.state !== "string") throw new CloudError("shape", "box");
    if (r.state === "busy") continue;
    if (r.state === "idle") throw new CloudError("network", "lost");
    if (r.state === "done") {
      if (typeof r.token !== "string" || !r.token) throw new CloudError("shape", "token");
      return r.token;
    }
    if (r.state === "failed") {
      switch (r.stage) {
        case "transport": case "http": throw new CloudError("network", r.stage);
        case "json": case "token": throw new CloudError("shape", r.stage);
        case "code":
          if (r.code === 10001) throw new CloudError("shape", "login request");
          throw new CloudError("credentials", undefined, Number.isInteger(r.code) ? r.code : undefined);
        default: throw new CloudError("shape", "box");
      }
    }
    throw new CloudError("shape", "box");
  }
  throw new CloudError("network", "timeout");
}

const STORE_STATES = ["ok", "pending", "save failed", "forget failed"];

// What the box holds: {saved, store, malformed}. saved is the saved sign-in's hint ({email_hint, region})
// or null; store is the last save or erase, "ok" | "pending" | "save failed" | "forget failed" ("unknown"
// for anything else). malformed: the idle reply lacked an explicit saved (null or a well-formed hint) or a
// known store, so it confirms nothing. null when the box is not idle or cannot be read. timeoutMs bounds
// the call. With rethrowPin the page can ask for the PIN (a CloudError box/pin) instead of showing the form.
export async function savedLogin({ fetchImpl = globalThis.fetch, headers = {}, rethrowPin = false, timeoutMs } = {}) {
  try {
    const r = await callBox(BOX_LOGIN, { method: "GET", headers: { ...headers } }, fetchImpl, timeoutMs);
    if (r.state === "idle") {
      const s = r.saved;
      const known = STORE_STATES.includes(r.store);
      const wellFormed = s !== null && typeof s === "object" && typeof s.email_hint === "string" && typeof s.region === "string";
      return {
        saved: wellFormed ? { email_hint: s.email_hint, region: s.region } : null,
        store: known ? r.store : "unknown",
        malformed: !known || !(s === null || wellFormed),
      };
    }
  } catch (e) {
    if (rethrowPin && e instanceof CloudError && e.kind === "box" && e.detail === "pin") throw e;
    /* otherwise the door shows the form */
  }
  return null;
}

// What the page may say after a Remember or a Forget: only what a well-formed reply with store "ok" confirms.
export const confirmsSaved = (st) => !!st && !st.malformed && st.store === "ok" && st.saved !== null;
export const confirmsForgotten = (st) => !!st && !st.malformed && st.store === "ok" && st.saved === null;

// Queues the erase on the box; savedLogin then reports store "pending" until it is written.
export async function forgetSaved({ fetchImpl = globalThis.fetch, headers = {} } = {}) {
  const r = await callBox(BOX_FORGET, { method: "POST", headers: jsonHeaders(headers), body: "{}" }, fetchImpl);
  if (r.ok !== true) throw new CloudError("box", typeof r.error === "string" ? r.error : "forget");
  return true;
}

// Polls savedLogin while the box's store is "pending", for at most timeoutMs in all, each read included.
// Returns the last reading (null when the box could not be read in time).
export async function settledStore({ fetchImpl = globalThis.fetch, headers = {}, timeoutMs = 3000,
  sleep = (ms) => new Promise((r) => setTimeout(r, ms)), now = Date.now } = {}) {
  const start = now();
  const left = () => timeoutMs - (now() - start);
  // Every read is bounded by what is left of the wait: a hung GET ends it (null, "did not confirm").
  let r = await savedLogin({ fetchImpl, headers, timeoutMs: left() });
  while (r && r.store === "pending" && left() > 0) {
    await sleep(250);
    if (left() <= 0) break;
    r = await savedLogin({ fetchImpl, headers, timeoutMs: left() });
  }
  return r;
}

export async function fetchMesh(regionId, token, fetchImpl = globalThis.fetch) {
  if (typeof token !== "string" || !token) throw new CloudError("shape", "token");
  const body = await callCloud(queryUrl(regionId), { headers: { Token: token } }, fetchImpl, "token");
  const data = body.data;
  if (!Array.isArray(data?.meshs?.existing)) throw new CloudError("shape", "meshs.existing");
  if (!Array.isArray(data?.fixtures?.existing)) throw new CloudError("shape", "fixtures.existing");
  return data;
}

export function normaliseMac(s) {
  if (typeof s !== "string") return null;
  const t = s.trim().toUpperCase();
  const hex = /^([0-9A-F]{2}:){5}[0-9A-F]{2}$/.test(t) ? t.replace(/:/g, "") : t;
  if (!/^[0-9A-F]{12}$/.test(hex)) return null;
  const mac = hex.match(/../g).join(":");
  return MAC_RE.test(mac) ? mac : null;
}

const lowerKey = (k) => String(k ?? "").trim().toLowerCase();

// A mesh identity is a non-empty string; anything else (missing, null, empty)
// must never compare equal to another missing one.
const meshId = (rec) => (typeof rec?.mesh_uuid === "string" && rec.mesh_uuid !== "" ? rec.mesh_uuid : null);

// Two records sharing an id would make a choice ambiguous (the first one's keys
// would be used for either): refuse before offering or extracting anything.
function assertUniqueMeshIds(pack) {
  const seen = new Set();
  for (const m of pack.meshs.existing) {
    const id = meshId(m);
    if (id === null) continue;
    if (seen.has(id)) throw new CloudError("shape", "duplicate mesh id");
    seen.add(id);
  }
}

export function meshChoices(pack) {
  assertUniqueMeshIds(pack);
  const out = [];
  pack.meshs.existing.forEach((m, i) => {
    const id = meshId(m);
    if (id === null) return;
    out.push({
      uuid: id,
      index: i + 1,
      // Counted the way toExtracted keeps them: duplicate MACs, duplicate addresses and id clashes once.
      fixtures: dedupe(
        pack.fixtures.existing
          .filter((f) => meshId(f) === id && parseInt(String(f.node_address), 10) >= 2 && normaliseMac(f.mac_address) !== null)
          .map((f) => ({ mac: normaliseMac(f.mac_address), address: parseInt(String(f.node_address), 10), _t: Number(f.update_time) || 0 })),
        (f) => f._t,
      ).kept.length,
      keysOk: HEX32.test(lowerKey(m.net_key)) && HEX32.test(lowerKey(m.app_key)),
    });
  });
  return out;
}

// The same object the database door's extract() returns, so the page and
// toImportPayload need no change. Per-fixture rules mirror extractUnsafe.
export function toExtracted(pack, meshUuid, capsCfg) {
  assertUniqueMeshIds(pack);
  const mesh = typeof meshUuid === "string" && meshUuid !== "" ? pack.meshs.existing.find((m) => meshId(m) === meshUuid) : undefined;
  if (!mesh) throw new CloudError("nomesh");
  const net_key = lowerKey(mesh.net_key);
  const app_key = lowerKey(mesh.app_key);
  if (!HEX32.test(net_key) || !HEX32.test(app_key)) throw new CloudError("shape", "mesh keys");

  const known = new Set(pack.meshs.existing.map(meshId).filter((id) => id !== null));
  const fixtures = [];
  const skipped = [];
  for (const f of pack.fixtures.existing) {
    const address = parseInt(String(f.node_address), 10);
    const name = boxName(String(f.name ?? "").trim() || `Light ${address}`);
    const mac = normaliseMac(f.mac_address);
    const fid = meshId(f);
    if (fid === null || !known.has(fid)) { skipped.push({ name, reason: "no mesh" }); continue; }
    if (fid !== meshUuid) continue;
    if (!(address >= 2)) { skipped.push({ name, reason: "bad address" }); continue; }
    if (!mac) { skipped.push({ name, reason: "bad MAC" }); continue; }
    const code = String(f.code ?? "").trim().toUpperCase();
    fixtures.push({ mac, address, name, code, hwver: "", device_tag: undefined, caps: resolveCaps(capsCfg, code, ""), _t: Number(f.update_time) || 0 });
  }
  const { kept, dropped } = dedupe(fixtures, (f) => f._t);
  for (const { f, reason } of dropped) skipped.push({ name: f.name, reason });
  for (const f of kept) delete f._t;
  kept.sort((a, b) => a.address - b.address);
  return {
    schema: "cloud",
    keys: { net_key, app_key, net_key_present: true, app_key_present: true, iv_index: 0 },
    fixtures: kept,
    skipped,
    groups: [],
  };
}

export function userMessage(err) {
  const kind = err instanceof CloudError ? err.kind : "other";
  switch (kind) {
    case "credentials":
      return "amaran did not accept that email and password for this region. If your account was created in another region, choose it and try again. Or import from the app's database instead.";
    case "token":
      return `amaran refused the fetch after sign-in (${Number.isInteger(err.code) ? "code " + err.code : "no code"}). Try again; if it keeps happening, import from the app's database instead.`;
    case "shape":
      if (err.detail === "login request") return "amaran refused the box's sign-in request (code 10001). The database import still works.";
      return `amaran's reply did not look like a mesh (missing ${err.detail}). The database import still works.`;
    case "nomesh":
      return "This account has no mesh with fixtures. Set your lights up in the amaran app first, or import from the app's database.";
    case "box":
      if (err.detail === "locked") return "This box is locked after too many wrong PINs. Wait half a minute and try again.";
      return err.detail === "pin"
        ? "This box asks for its PIN before it signs in. Enter it and try again."
        : `The box could not start the sign-in (${err.detail}). Reload the page and try again.`;
    case "network":
      return "The box could not reach amaran's servers. On its own setup Wi-Fi there is no internet: join your home network, or import from the app's database instead.";
    default:
      return "Something in amaran's reply was not what this page expected. The database import still works.";
  }
}
