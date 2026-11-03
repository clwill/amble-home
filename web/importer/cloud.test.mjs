import test from "node:test";
import assert from "node:assert/strict";
import { REGIONS, queryUrl, QUERY_CURSORS, loginViaBox, savedLogin, forgetSaved, settledStore, confirmsSaved, confirmsForgotten, fetchMesh, CloudError, userMessage, meshChoices, toExtracted, normaliseMac } from "./cloud.js";
import { toImportPayload } from "./importer.js";

const okJson = (body, status = 200) => ({ ok: status < 400, status, json: async () => body });
const fakeFetch = (reply) => { const calls = []; const f = async (url, init) => { calls.push({ url, init }); return typeof reply === "function" ? reply(url, init) : reply; }; f.calls = calls; return f; };

test("regions and urls", () => {
  assert.deepEqual(REGIONS.map((r) => [r.id, r.area]), [["us", "US"], ["eu", "EU"], ["cn", "CN"]]);
  assert.equal(QUERY_CURSORS.length, 11);
  const names = "mesh, fixture, scene, group, quickshot, preset, scene_detail, group_detail, quickshot_detail, camera, camera_preset".split(", ");
  assert.equal(queryUrl("us"), "https://us.sidus.link/amaran/pack/query?" + names.map((c) => `${c}_update_time=0`).join("&") + "&attr_tags=fixture,camera,microphone");
  assert.throws(() => queryUrl("gs"), RangeError);
});

const PIN = { "X-Amble-Pin": "1234" };
// Scripted box: first reply answers the POST, the rest answer the GETs in order.
const boxFetch = (post, gets) => {
  const calls = []; let i = 0;
  const f = async (url, init = {}) => {
    calls.push({ url, method: init.method ?? "GET", headers: init.headers ?? {}, body: init.body });
    const r = (init.method ?? "GET") === "POST" ? post : gets[Math.min(i++, gets.length - 1)];
    if (r instanceof Error) throw r;
    return r;
  };
  f.calls = calls; return f;
};
const sleep = async () => {};
const queued = okJson({ ok: true, state: "queued" });

test("loginViaBox: busy, busy, done returns the token; PIN header on POST and every GET", async () => {
  const f = boxFetch(queued, [okJson({ state: "busy" }), okJson({ state: "busy" }), okJson({ state: "done", token: "tok123" })]);
  assert.equal(await loginViaBox("us", "  a@b.c ", " pw ", { fetchImpl: f, headers: PIN, sleep, remember: 1 }), "tok123");
  assert.deepEqual(f.calls.map((c) => [c.method, c.url]), [["POST", "/amble/cloud/login"], ["GET", "/amble/cloud/login"], ["GET", "/amble/cloud/login"], ["GET", "/amble/cloud/login"]]);
  assert.deepEqual(JSON.parse(f.calls[0].body), { email: "a@b.c", password: " pw ", region: "us", remember: true });
  assert.equal(f.calls[0].headers["Content-Type"], "application/json");
  for (const c of f.calls) assert.equal(c.headers["X-Amble-Pin"], "1234");
});

test("loginViaBox: saved sends {saved:true}; remember defaults false", async () => {
  const f = boxFetch(queued, [okJson({ state: "done", token: "t" })]);
  await loginViaBox("us", "", "", { fetchImpl: f, saved: true, sleep });
  assert.deepEqual(JSON.parse(f.calls[0].body), { saved: true });
  const g = boxFetch(queued, [okJson({ state: "done", token: "t" })]);
  await loginViaBox("eu", "a@b.c", "x", { fetchImpl: g, sleep });
  assert.equal(JSON.parse(g.calls[0].body).remember, false);
});

test("loginViaBox: box refusals", async () => {
  const go = (post, gets = []) => loginViaBox("us", "a@b.c", "x", { fetchImpl: boxFetch(post, gets), sleep });
  await assert.rejects(go(okJson({ ok: false, error: "pin required" }, 401)), (e) => e.kind === "box" && e.detail === "pin");
  await assert.rejects(go(okJson({ ok: false, error: "pin wrong" }, 401)), (e) => e.kind === "box" && e.detail === "pin");
  await assert.rejects(go(okJson({ ok: false, error: "a sign-in is already running" }, 409)), (e) => e.kind === "box" && e.detail === "a sign-in is already running");
  await assert.rejects(go(okJson({ ok: false, error: "no saved sign-in" }, 400)), (e) => e.kind === "box" && e.detail === "no saved sign-in");
  await assert.rejects(go(new TypeError("Failed to fetch")), (e) => e.kind === "network");
  await assert.rejects(go({ ok: true, status: 200, json: async () => { throw new SyntaxError(); } }), (e) => e.kind === "network");
});

test("loginViaBox: failed replies map to kinds", async () => {
  const fail = (o) => loginViaBox("us", "a@b.c", "x", { fetchImpl: boxFetch(queued, [okJson({ state: "failed", ...o })]), sleep });
  await assert.rejects(fail({ stage: "code", code: 20014, status: 200 }), (e) => e.kind === "credentials" && e.code === 20014);
  await assert.rejects(fail({ stage: "code", code: 10001 }), (e) => e.kind === "shape" && e.detail === "login request");
  await assert.rejects(fail({ stage: "transport" }), (e) => e.kind === "network" && e.detail === "transport");
  await assert.rejects(fail({ stage: "http", status: 502 }), (e) => e.kind === "network" && e.detail === "http");
  await assert.rejects(fail({ stage: "json" }), (e) => e.kind === "shape" && e.detail === "json");
  await assert.rejects(fail({ stage: "token" }), (e) => e.kind === "shape" && e.detail === "token");
  await assert.rejects(fail({ stage: "weird" }), (e) => e.kind === "shape" && e.detail === "box");
  await assert.rejects(fail({}), (e) => e.kind === "shape" && e.detail === "box");
});

test("loginViaBox: bad done, no-state, first-GET idle, GET failure, timeout", async () => {
  const run = (gets, extra = {}) => loginViaBox("us", "a@b.c", "x", { fetchImpl: boxFetch(queued, gets), sleep, ...extra });
  await assert.rejects(run([okJson({ state: "done", token: "" })]), (e) => e.kind === "shape" && e.detail === "token");
  await assert.rejects(run([okJson({ state: "done" })]), (e) => e.kind === "shape" && e.detail === "token");
  await assert.rejects(run([okJson({ nope: 1 })]), (e) => e.kind === "shape" && e.detail === "box");
  await assert.rejects(run([okJson({ state: "idle", saved: null })]), (e) => e.kind === "network" && e.detail === "lost");
  await assert.rejects(run([new TypeError("x")]), (e) => e.kind === "network");
  let t = 0;
  await assert.rejects(run([okJson({ state: "busy" })], { timeoutMs: 2000, now: () => t, sleep: async (ms) => { t += ms; } }), (e) => e.kind === "network" && e.detail === "timeout");
});

test("loginViaBox: a hung fetch is cut off at the deadline (POST and GET)", async () => {
  const hang = () => new Promise(() => {});
  await assert.rejects(loginViaBox("us", "a@b.c", "x", { fetchImpl: hang, sleep, timeoutMs: 50 }), (e) => e.kind === "network" && e.detail === "timeout");
  const f = boxFetch(queued, [new Promise(() => {})]);
  const g = async (url, init) => (init.method === "POST" ? f(url, init) : hang());
  await assert.rejects(loginViaBox("us", "a@b.c", "x", { fetchImpl: g, sleep, timeoutMs: 50 }), (e) => e.kind === "network" && e.detail === "timeout");
});

test("loginViaBox: a fetch that rejects with AbortError on its signal is network/timeout", async () => {
  const f = (url, init) => new Promise((_, rej) => {
    const err = () => { const e = new Error("aborted"); e.name = "AbortError"; rej(e); };
    if (init.signal) init.signal.addEventListener("abort", err); else err();
  });
  await assert.rejects(loginViaBox("us", "a@b.c", "x", { fetchImpl: f, sleep, timeoutMs: 50 }), (e) => e.kind === "network" && e.detail === "timeout");
});

test("loginViaBox: locked PIN, non-JSON 401, odd POST replies, non-string email", async () => {
  const go = (post, o = {}) => loginViaBox("us", o.email ?? "a@b.c", "x", { fetchImpl: boxFetch(post, [okJson({ state: "busy" })]), sleep, timeoutMs: 600, ...o });
  await assert.rejects(go(okJson({ ok: false, error: "locked" }, 401)), (e) => e.kind === "box" && e.detail === "locked");
  await assert.rejects(go({ ok: false, status: 401, json: async () => { throw new SyntaxError(); } }), (e) => e.kind === "box" && e.detail === "pin");
  await assert.rejects(go(okJson({ ok: true, state: "running" })), (e) => e.kind === "shape" && e.detail === "box");
  await assert.rejects(go(okJson({ state: "queued" })), (e) => e.kind === "shape" && e.detail === "box");
  const f = boxFetch(queued, [okJson({ state: "done", token: "t" })]);
  await loginViaBox("us", undefined, "x", { fetchImpl: f, sleep });
  assert.equal(JSON.parse(f.calls[0].body).email, "");
});

test("loginViaBox: errors never carry credentials or tokens", async () => {
  const f = boxFetch(queued, [okJson({ state: "failed", stage: "code", code: 20014 })]);
  try { await loginViaBox("us", "secret@b.c", "hunter2", { fetchImpl: f, sleep }); assert.fail(); } catch (e) {
    assert.doesNotMatch(JSON.stringify({ m: e.message, d: e.detail }), /secret|hunter2/);
  }
});

test("userMessage: login request refusal has its own copy; other shapes keep the mesh copy", () => {
  assert.equal(userMessage(new CloudError("shape", "login request")), "amaran refused the box's sign-in request (code 10001). The database import still works.");
  assert.equal(userMessage(new CloudError("shape", "token")), "amaran's reply did not look like a mesh (missing token). The database import still works.");
});
test("savedLogin rethrows a PIN refusal only when asked", async () => {
  await assert.rejects(savedLogin({ fetchImpl: boxFetch(null, [okJson({}, 401)]), rethrowPin: true }), (e) => e.kind === "box" && e.detail === "pin");
  assert.equal(await savedLogin({ fetchImpl: boxFetch(null, [okJson({ error: "locked" }, 401)]), rethrowPin: true }), null);
});
test("savedLogin returns {saved, store, malformed} when idle, else null", async () => {
  const hint = { email_hint: "c…@example.com", region: "us" };
  const f = boxFetch(null, [okJson({ state: "idle", saved: hint, store: "ok" })]);
  assert.deepEqual(await savedLogin({ fetchImpl: f, headers: PIN }), { saved: hint, store: "ok", malformed: false });
  assert.equal(f.calls[0].url, "/amble/cloud/login"); assert.equal(f.calls[0].headers["X-Amble-Pin"], "1234");
  assert.deepEqual(await savedLogin({ fetchImpl: boxFetch(null, [okJson({ state: "idle", saved: null, store: "ok" })]) }), { saved: null, store: "ok", malformed: false });
  for (const store of ["pending", "save failed", "forget failed"])
    assert.deepEqual(await savedLogin({ fetchImpl: boxFetch(null, [okJson({ state: "idle", saved: null, store })]) }), { saved: null, store, malformed: false });
  assert.deepEqual(await savedLogin({ fetchImpl: boxFetch(null, [okJson({ state: "idle", saved: null })]) }), { saved: null, store: "unknown", malformed: true });
  assert.deepEqual(await savedLogin({ fetchImpl: boxFetch(null, [okJson({ state: "idle", saved: { email_hint: 5 }, store: "ok" })]) }), { saved: null, store: "ok", malformed: true });
  assert.deepEqual(await savedLogin({ fetchImpl: boxFetch(null, [okJson({ state: "idle", store: "ok" })]) }), { saved: null, store: "ok", malformed: true });
  assert.deepEqual(await savedLogin({ fetchImpl: boxFetch(null, [okJson({ state: "idle", saved: "x", store: "ok" })]) }), { saved: null, store: "ok", malformed: true });
  assert.equal(await savedLogin({ fetchImpl: boxFetch(null, [new TypeError("x")]) }), null);
  assert.equal(await savedLogin({ fetchImpl: boxFetch(null, [okJson({ state: "busy" })]) }), null);
  assert.equal(await savedLogin({ fetchImpl: boxFetch(null, [okJson({}, 401)]) }), null);
});
test("confirmsSaved / confirmsForgotten need a well-formed reply with store ok", async () => {
  const hint = { email_hint: "c…@example.com", region: "us" };
  const read = async (body) => savedLogin({ fetchImpl: boxFetch(null, [okJson(body)]) });
  assert.equal(confirmsForgotten(await read({ state: "idle", saved: null, store: "ok" })), true);
  assert.equal(confirmsForgotten(await read({ state: "idle", saved: null })), false);  // no store
  assert.equal(confirmsForgotten(await read({ state: "idle", saved: { email_hint: 5 }, store: "ok" })), false);  // malformed saved
  assert.equal(confirmsForgotten(await read({ state: "idle", store: "ok" })), false);  // saved missing, not null
  assert.equal(confirmsForgotten(await read({ state: "idle", saved: null, store: "pending" })), false);
  assert.equal(confirmsForgotten(await read({ state: "idle", saved: null, store: "forget failed" })), false);
  assert.equal(confirmsForgotten(await read({ state: "idle", saved: hint, store: "ok" })), false);
  assert.equal(confirmsForgotten(null), false);
  assert.equal(confirmsSaved(await read({ state: "idle", saved: hint, store: "ok" })), true);
  assert.equal(confirmsSaved(await read({ state: "idle", saved: hint })), false);  // no store
  assert.equal(confirmsSaved(await read({ state: "idle", saved: { email_hint: "c…@example.com" }, store: "ok" })), false);  // malformed
  assert.equal(confirmsSaved(await read({ state: "idle", saved: hint, store: "pending" })), false);
  assert.equal(confirmsSaved(await read({ state: "idle", saved: null, store: "ok" })), false);
  assert.equal(confirmsSaved(null), false);
});
test("settledStore polls while pending, stops at the first settled reading or the deadline", async () => {
  const hint = { email_hint: "c…@example.com", region: "us" };
  const pend = okJson({ state: "idle", saved: hint, store: "pending" });
  let f = boxFetch(null, [pend, pend, okJson({ state: "idle", saved: hint, store: "ok" })]);
  assert.deepEqual(await settledStore({ fetchImpl: f, headers: PIN, sleep }), { saved: hint, store: "ok", malformed: false });
  assert.equal(f.calls.length, 3); assert.ok(f.calls.every((c) => c.headers["X-Amble-Pin"] === "1234"));
  f = boxFetch(null, [okJson({ state: "idle", saved: null, store: "save failed" })]);
  assert.deepEqual(await settledStore({ fetchImpl: f, sleep }), { saved: null, store: "save failed", malformed: false });
  assert.equal(f.calls.length, 1);
  let t = 0;
  f = boxFetch(null, [pend]);
  assert.deepEqual(await settledStore({ fetchImpl: f, timeoutMs: 1000, now: () => t, sleep: async (ms) => { t += ms; } }), { saved: hint, store: "pending", malformed: false });
  assert.equal(f.calls.length, 4);  // reads at 0, 250, 500, 750 ms; none at the deadline itself
  assert.equal(await settledStore({ fetchImpl: boxFetch(null, [new TypeError("x")]), sleep }), null);
});
test("settledStore: a hung GET ends the wait at its deadline", async () => {
  const hang = () => new Promise(() => {});
  const t0 = Date.now();
  assert.equal(await settledStore({ fetchImpl: hang, timeoutMs: 50 }), null);
  assert.ok(Date.now() - t0 < 1000);
  // A first reading of pending, then a GET that never answers: still ends near the deadline.
  let n = 0;
  const pendThenHang = async () => (n++ === 0 ? okJson({ state: "idle", saved: null, store: "pending" }) : new Promise(() => {}));
  const t1 = Date.now();
  assert.equal(await settledStore({ fetchImpl: pendThenHang, timeoutMs: 300, sleep: async () => {} }), null);
  assert.ok(Date.now() - t1 < 1500);
});
test("loginViaBox: the default deadline is 40 s, past the box's own 20 s", async () => {
  let t = 0;
  const f = boxFetch(queued, [okJson({ state: "busy" })]);
  await assert.rejects(loginViaBox("us", "a@b.c", "x", { fetchImpl: f, now: () => t, sleep: async (ms) => { t += ms; } }), (e) => e.kind === "network" && e.detail === "timeout");
  assert.equal(t, 40000);
});

test("forgetSaved posts {} and resolves true; failure throws box", async () => {
  const f = boxFetch(okJson({ ok: true, state: "queued" }), []);
  assert.equal(await forgetSaved({ fetchImpl: f, headers: PIN }), true);
  const c = f.calls[0];
  assert.equal(c.method, "POST"); assert.equal(c.url, "/amble/cloud/forget"); assert.equal(c.body, "{}");
  assert.equal(c.headers["Content-Type"], "application/json"); assert.equal(c.headers["X-Amble-Pin"], "1234");
  await assert.rejects(forgetSaved({ fetchImpl: boxFetch(okJson({ ok: false, error: "pin wrong" }, 401), []) }), (e) => e.kind === "box" && e.detail === "pin");
  await assert.rejects(forgetSaved({ fetchImpl: boxFetch(okJson({ ok: false, error: "bad" }, 400), []) }), (e) => e.kind === "box" && e.detail === "bad");
  await assert.rejects(forgetSaved({ fetchImpl: boxFetch(new TypeError("x"), []) }), (e) => e.kind === "network");
});

test("userMessage for box and network", () => {
  assert.equal(userMessage(new CloudError("box", "locked")), "This box is locked after too many wrong PINs. Wait half a minute and try again.");
  assert.equal(userMessage(new CloudError("box", "pin")), "This box asks for its PIN before it signs in. Enter it and try again.");
  assert.equal(userMessage(new CloudError("box", "a sign-in is already running")), "The box could not start the sign-in (a sign-in is already running). Reload the page and try again.");
  assert.equal(userMessage(new CloudError("network")), "The box could not reach amaran's servers. On its own setup Wi-Fi there is no internet: join your home network, or import from the app's database instead.");
});

test("user messages never echo the server", () => {
  const m = userMessage(new CloudError("credentials", "密码错误"));
  assert.match(m, /did not accept that email and password/); assert.match(m, /database/); assert.doesNotMatch(m, /密码/);
  assert.doesNotMatch(userMessage(new TypeError("Failed to fetch")), /could not reach amaran/);
  assert.match(userMessage(new TypeError("Failed to fetch")), /Something in amaran's reply was not what this page expected/);
  assert.match(userMessage(new CloudError("network")), /could not reach amaran/);
  assert.match(userMessage(new CloudError("token")), /\(no code\)/);
  assert.doesNotMatch(userMessage(new CloudError("token")), /undefined/);
  assert.match(userMessage(new CloudError("token", "x", 20003)), /20003/);
});

const pack = (over = {}) => ({ code: 20000, msg: "ok", data: { meshs: { deleted: [], existing: [] }, fixtures: { deleted: [], existing: [] }, ...over } });

test("fetchMesh sends the Token header on the proven query and returns data", async () => {
  const f = fakeFetch(okJson(pack()));
  const d = await fetchMesh("eu", "tok123", f);
  assert.equal(f.calls[0].url, queryUrl("eu"));
  assert.equal((f.calls[0].init.method ?? "GET"), "GET");
  assert.deepEqual(f.calls[0].init.headers, { Token: "tok123" });
  assert.ok(Array.isArray(d.meshs.existing) && Array.isArray(d.fixtures.existing));
});

test("fetchMesh rejections", async () => {
  await assert.rejects(fetchMesh("us", "t", fakeFetch(okJson({ code: 20003, msg: "token expired" }))), (e) => e.kind === "token" && e.code === 20003);
  await assert.rejects(fetchMesh("us", "t", fakeFetch(okJson({ code: 20000, data: { meshs: {} } }))), (e) => e.kind === "shape" && /meshs\.existing/.test(e.detail));
  await assert.rejects(fetchMesh("us", "t", fakeFetch(okJson({ code: 20000, data: { meshs: { existing: [] } } }))), (e) => e.kind === "shape" && /fixtures/.test(e.detail));
  await assert.rejects(fetchMesh("us", "t", fakeFetch(() => { throw new TypeError("Failed to fetch"); })), (e) => e.kind === "network");
  await assert.rejects(fetchMesh("us", "t", fakeFetch(okJson({}, 502))), (e) => e.kind === "network");
  await assert.rejects(fetchMesh("us", "t", fakeFetch({ ok: true, status: 200, json: async () => { throw new SyntaxError(); } })), (e) => e.kind === "shape");
  await assert.rejects(fetchMesh("us", "", fakeFetch(okJson(pack()))), (e) => e.kind === "shape" && e.detail === "token");
});

const K1 = "0123456789abcdef0123456789abcdef", K2 = "fedcba9876543210fedcba9876543210";
const mesh = (uuid, net_key = K1.toUpperCase(), app_key = K2) => ({ user_id: "u", mesh_uuid: uuid, net_key, app_key, fixtures_ordered_list: "", scenes_ordered_list: "", update_time: 1 });
const fx = (mesh_uuid, node_address, mac_address, name = "L", code = "401C5") => ({ user_id: "u", mesh_uuid, node_address, mac_address, code, node_name: "n", name, composition_data: "", device_uuid: "d", device_key: "k", binding: 1, fast_provision_supported: 0, update_time: 1, platform: 1, update_platform: 7 });
const caps = { "fixture_401C5_1.0": { hsi_support: "1", gm_support: "1", product_cct_min: "25", product_cct_max: "75" } };
const data = {
  meshs: { deleted: [{ mesh_uuid: "gone" }], existing: [mesh("m1"), mesh("m2", "nothex", K2)] },
  fixtures: { deleted: [], existing: [
    fx("m1", 27, "AA:BB:CC:8D:ED:B3", "amaran P60c"),
    fx("m1", 3, "aabbcc000001", ""),            // lower case, no colons, empty name
    fx("m1", 1, "AA:BB:CC:00:00:02", "prov"),   // address 1: skipped
    fx("m1", 9, "AA:BB:CC:00:00:0", "short"),  // bad MAC: skipped
    fx("gone", 5, "AA:BB:CC:00:00:05", "orphan"),
    fx("m2", 6, "AA:BB:CC:00:00:06", "other mesh"),
  ] },
};

test("normaliseMac", () => {
  assert.equal(normaliseMac("aabbcc8dedb3"), "AA:BB:CC:8D:ED:B3");
  assert.equal(normaliseMac(" aa:bb:cc:8d:ed:b3 "), "AA:BB:CC:8D:ED:B3");
  assert.equal(normaliseMac("AA:BB:CC:00:00:0"), null); assert.equal(normaliseMac(undefined), null);
});

test("meshChoices counts usable fixtures per existing mesh and flags bad keys", () => {
  assert.deepEqual(meshChoices(data), [{ uuid: "m1", index: 1, fixtures: 2, keysOk: true }, { uuid: "m2", index: 2, fixtures: 1, keysOk: false }]);
});

test("toExtracted matches the database extractor's shape", () => {
  const r = toExtracted(data, "m1", caps);
  assert.deepEqual(r.keys, { net_key: K1, app_key: K2, net_key_present: true, app_key_present: true, iv_index: 0 });
  assert.deepEqual(r.fixtures.map((f) => [f.mac, f.address, f.name]), [["AA:BB:CC:00:00:01", 3, "Light 3"], ["AA:BB:CC:8D:ED:B3", 27, "amaran P60c"]]);
  assert.deepEqual(r.fixtures[1].caps, { color: true, gm: true, cct_min: 2500, cct_max: 7500, src: "fixture_401C5_1.0", source: "fixture_config" });
  assert.equal(r.fixtures[0].hwver, ""); assert.deepEqual(r.groups, []); assert.equal(r.schema, "cloud");
  assert.deepEqual(r.skipped, [{ name: "prov", reason: "bad address" }, { name: "short", reason: "bad MAC" }, { name: "orphan", reason: "no mesh" }]);
  assert.ok(!r.fixtures.some((f) => f.name === "other mesh"));
  assert.equal(toImportPayload(r).fixtures.length, 2);
});

test("toExtracted refuses a missing mesh and bad keys", () => {
  assert.throws(() => toExtracted(data, "m9", caps), (e) => e.kind === "nomesh");
  assert.throws(() => toExtracted(data, "m2", caps), (e) => e.kind === "shape" && e.detail === "mesh keys");
});

test("toExtracted gives the box's truncated name (31 bytes, whole characters)", () => {
  const d = { meshs: { existing: [mesh("m1")] }, fixtures: { existing: [fx("m1", 3, "AA:BB:CC:00:00:01", "灯".repeat(11))] } };
  assert.equal(toExtracted(d, "m1", caps).fixtures[0].name, "灯".repeat(10));
});

test("a non-integer or missing server code is a shape error, never echoed", async () => {
  for (const code of ["20003", { a: 1 }, null, undefined]) {
    const body = code === undefined ? { msg: "x" } : { code, msg: "x" };
    await assert.rejects(fetchMesh("us", "t", fakeFetch(okJson(body))), (e) => e.kind === "shape" && e.detail === "code" && e.code === undefined);
  }
  await assert.rejects(fetchMesh("us", "t", fakeFetch(okJson({ code: 20003 }))), (e) => e.kind === "token" && e.code === 20003 && /code 20003/.test(userMessage(e)));
  assert.match(userMessage(new CloudError("token", undefined, "<b>x</b>")), /\(no code\)/);
});

test("missing or empty mesh identities fail closed", () => {
  for (const id of [undefined, null, ""]) {
    const m = { net_key: K1, app_key: K2 }; if (id !== undefined) m.mesh_uuid = id;
    const f = { ...fx("x", 3, "AA:BB:CC:00:00:01"), mesh_uuid: id }; if (id === undefined) delete f.mesh_uuid;
    const d = { meshs: { existing: [m] }, fixtures: { existing: [f] } };
    assert.deepEqual(meshChoices(d), []);
    assert.throws(() => toExtracted(d, id, caps), (e) => e.kind === "nomesh");
  }
  const d = { meshs: { existing: [mesh("m1")] }, fixtures: { existing: [{ ...fx("m1", 3, "AA:BB:CC:00:00:01", "Nul"), mesh_uuid: null }, fx("m1", 4, "AA:BB:CC:00:00:02", "Ok")] } };
  const r = toExtracted(d, "m1", caps);
  assert.deepEqual(r.skipped, [{ name: "Nul", reason: "no mesh" }]);
  assert.deepEqual(r.fixtures.map((f) => f.name), ["Ok"]);
});

test("a broken record in another existing mesh is not counted against the chosen mesh", () => {
  const d = { meshs: { existing: [mesh("m1"), mesh("m2")] }, fixtures: { existing: [fx("m1", 3, "AA:BB:CC:00:00:01", "Good"), fx("m2", 1, "AA:BB:CC:00:00:02", "OtherBadAddr"), fx("m2", 6, "bad", "OtherBadMac")] } };
  assert.deepEqual(toExtracted(d, "m1", caps).skipped, []);
});

test("duplicate mesh ids are refused before any choice or extraction", () => {
  const d = { meshs: { existing: [mesh("m1"), mesh("m1", K2, K1)] }, fixtures: { existing: [fx("m1", 3, "AA:BB:CC:00:00:01", "One")] } };
  const shape = (e) => e.kind === "shape" && e.detail === "duplicate mesh id";
  assert.throws(() => meshChoices(d), shape);
  assert.throws(() => toExtracted(d, "m1", caps), shape);
});

// The account door applies the same de-duplication as the database door.
test("toExtracted keeps the newest of two records sharing a MAC and skips the other as duplicate MAC", () => {
  const older = { ...fx("m1", 3, "AA:BB:CC:00:00:01", "Old"), update_time: 5 };
  const newer = { ...fx("m1", 7, "AA:BB:CC:00:00:01", "New"), update_time: 9 };
  const r = toExtracted({ meshs: { existing: [mesh("m1")] }, fixtures: { existing: [newer, older] } }, "m1", caps);
  assert.deepEqual(r.fixtures.map((f) => f.name), ["New"]);
  assert.deepEqual(r.skipped, [{ name: "Old", reason: "duplicate MAC" }]);
  assert.ok(!("_t" in r.fixtures[0]));
});

test("toExtracted skips the second of two different MACs that would share an entity id", () => {
  const d = { meshs: { existing: [mesh("m1")] }, fixtures: { existing: [fx("m1", 3, "AA:BB:CC:00:00:01", "First"), fx("m1", 4, "11:22:33:00:00:01", "Clash")] } };
  const r = toExtracted(d, "m1", caps);
  assert.deepEqual(r.fixtures.map((f) => f.name), ["First"]);
  assert.deepEqual(r.skipped, [{ name: "Clash", reason: "id clash" }]);
});

// The account door settles two fixtures on one mesh address the same way.
test("toExtracted keeps the newest of two records sharing a mesh address and skips the other as duplicate address", () => {
  const older = { ...fx("m1", 3, "AA:BB:CC:00:00:01", "Old"), update_time: 5 };
  const newer = { ...fx("m1", 3, "AA:BB:CC:00:00:02", "New"), update_time: 9 };
  const r = toExtracted({ meshs: { existing: [mesh("m1")] }, fixtures: { existing: [older, newer] } }, "m1", caps);
  assert.deepEqual(r.fixtures.map((f) => f.name), ["New"]);
  assert.deepEqual(r.skipped, [{ name: "Old", reason: "duplicate address" }]);
});

test("meshChoices counts a duplicate address once, like toExtracted", () => {
  const d = { meshs: { existing: [mesh("m1")] }, fixtures: { existing: [fx("m1", 3, "AA:BB:CC:00:00:01", "One"), { ...fx("m1", 3, "AA:BB:CC:00:00:02", "Two same address"), update_time: 1 }] } };
  assert.equal(meshChoices(d)[0].fixtures, 1);
  assert.equal(toExtracted(d, "m1", caps).fixtures.length, 1);
});

test("meshChoices counts fixtures the way toExtracted keeps them (duplicates and clashes counted once)", () => {
  const d = { meshs: { existing: [mesh("m1")] }, fixtures: { existing: [
    fx("m1", 3, "AA:BB:CC:00:00:01", "One"), { ...fx("m1", 9, "AA:BB:CC:00:00:01", "One again"), update_time: 0 }, fx("m1", 4, "11:22:33:00:00:01", "Clash"), fx("m1", 5, "AA:BB:CC:00:00:03", "Three"),
  ] } };
  assert.equal(meshChoices(d)[0].fixtures, 2);
  assert.equal(toExtracted(d, "m1", caps).fixtures.length, 2);
});
