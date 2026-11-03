#!/usr/bin/env node
// Dev tool: drive the Edit page's three doors in headless Chrome against a local
// stand-in, and assert the cloud door's promises. Checks: the three doors exist;
// the region list; take-over has no controls; a failed sign-in clears the password
// field in the same tick it says "Signing in…", shows the credentials copy and
// leaves the page's own message alone; an offline failure shows the network copy;
// an account with two meshes shows the picker; a normal fetch lists rows, pre-ticks
// the fixture the box already has, and shows the change line and the skipped line;
// then the database door still takes a file (spikes/03-importer/amaran.db, skipped
// with a note if absent).
//
//   node web/box/build.mjs && node tools/door-check.mjs
//
// It never contacts amaran or a box: the page is served from web/box/dist/ by a local
// server, /amble/status is a canned object, and /amble/cloud.js is the real module
// with fetchMesh replaced by scripted replies keyed on the token; the sign-in itself is
// the real loginViaBox talking to the stand-in's /amble/cloud/login and /forget routes,
// whose outcomes are scripted by the email typed. Every scenario also asserts the
// password field is empty before the first box request and that the password appears
// in exactly one request (the POST to /amble/cloud/login) and nowhere else.
// Needs Google Chrome in /Applications and puppeteer-core (cd web/importer && npm ci).
// Prints names and counts only; exits non-zero on any failed assertion.
import fs from "node:fs";
import http from "node:http";
import path from "node:path";
import { createRequire } from "node:module";
import { fileURLToPath } from "node:url";

const root = path.resolve(path.dirname(fileURLToPath(import.meta.url)), "..");
const puppeteer = createRequire(root + "/web/importer/package.json")("puppeteer-core");
const dist = root + "/web/box/dist";
if (!fs.existsSync(dist + "/cloud.js")) { console.error("web/box/dist/cloud.js missing: run `node web/box/build.mjs` (with the cloud door on) first"); process.exit(2); }

// Canned box: two configured fixtures. amble_000001 is in the fetched list; amble_000002 is not.
const status = {
  name: "amble-home-000000", configured: true, pin_set: false,
  mesh: { up: true, own: 4097 },
  fixtures: [
    { id: "amble_000001", name: "Fixture One", live: true },
    { id: "amble_000002", name: "Old Fixture", live: false },
  ],
};

// The real cloud.js with its two network calls replaced; everything else is the real code.
const K1 = "0123456789abcdef0123456789abcdef", K2 = "fedcba9876543210fedcba9876543210";
const fakeCloud = fs.readFileSync(root + "/web/importer/cloud.js", "utf8")
  .replace("export async function fetchMesh(", "async function _realFetchMesh(") + `
const K1 = "${K1}", K2 = "${K2}";
const mesh = (uuid) => ({ mesh_uuid: uuid, net_key: K1, app_key: K2 });
const fx = (mesh_uuid, node_address, mac_address, name) => ({ mesh_uuid, node_address, mac_address, name, code: "401C5", update_time: 2 });
export async function fetchMesh(region, token) {
  const base = [
    fx("m1", 3, "AA:BB:CC:00:00:01", "Fixture One"),
    fx("m1", 5, "AA:BB:CC:00:00:03", "Fixture Three"),
    fx("m1", 7, "AA:BB:CC:00:0", "Broken Record"),
    { ...fx("m1", 9, "AA:BB:CC:00:00:03", "Fixture Three Again"), update_time: 1 },  // same MAC as Fixture Three, older: skipped (#34)
    { ...fx("m1", 5, "AA:BB:CC:00:00:05", "Fixture Five On Three's Address"), update_time: 1 },  // same address as Fixture Three, older: skipped (#36)
  ];
  if (token === "two@example.invalid") {
    return { meshs: { existing: [mesh("m1"), mesh("m2")] }, fixtures: { existing: [...base, fx("m2", 4, "AA:BB:CC:00:00:09", "Other Mesh Light")] } };
  }
  if (token === "onegood@example.invalid") {
    return { meshs: { existing: [mesh("m1"), mesh("m3")] }, fixtures: { existing: base } };
  }
  return { meshs: { existing: [mesh("m1")] }, fixtures: { existing: base } };
}
`;

const PASSWORD = "not-a-real-password";
const types = { ".js": "text/javascript", ".html": "text/html", ".json": "application/json", ".wasm": "application/wasm" };
// The box's three routes. Outcomes are keyed on the email in the POST body; the token a
// successful sign-in hands back is that email (or the saved sign-in's), which fetchMesh reads.
const SAVED_HINT = { email_hint: "s…@example.invalid", region: "eu" };
// Storage is modelled as the firmware does it: a save or an erase is queued, the idle GET reports
// store "pending" for one poll, then "ok" (or "save failed" / "forget failed" when box.fail says so).
const box = { saved: null, pending: null, reqs: [], requirePin: false, refused: 0, store: "ok", op: null, fail: { save: false, forget: false } };
const resetBox = (saved = null) => { box.saved = saved; box.pending = null; box.store = "ok"; box.op = null; box.fail = { save: false, forget: false }; box.omitStore = false; };
const idleReply = () => {
  const op = box.op;
  if (op && op.left-- > 0) return { ok: true, state: "idle", saved: op.kind === "save" ? op.hint : box.saved, store: "pending" };
  if (op) {
    box.op = null;
    if (op.kind === "save") { if (box.fail.save) box.store = "save failed"; else { box.saved = op.hint; box.store = "ok"; } }
    else if (box.fail.forget) box.store = "forget failed";
    else { box.saved = null; box.store = "ok"; }
  }
  const r = { ok: true, state: "idle", saved: box.saved, store: box.store };
  if (box.omitStore) delete r.store;  // an older or broken box: confirms nothing
  return r;
};
const outcomeFor = (email) => {
  if (email === "wrong@example.invalid") return { state: "failed", stage: "code", code: 20014 };
  if (email === "offline@example.invalid") return { state: "failed", stage: "transport" };
  if (email === "malformed@example.invalid") return { state: "failed", stage: "code", code: 10001 };
  if (email === "slowfail@example.invalid") return { state: "failed", stage: "code", code: 20014, wait: 1500 };
  if (email === "slow@example.invalid") return { state: "done", token: email, wait: 1500 };
  return { state: "done", token: email };
};
const json = (s, code, obj) => { s.statusCode = code; s.setHeader("content-type", "application/json"); s.end(JSON.stringify(obj)); };
const srv = http.createServer((q, s) => {
  const u = q.url.split("?")[0];
  let body = "";
  q.on("data", (c) => (body += c));
  q.on("end", () => {
    box.reqs.push({ method: q.method, url: q.url, headers: JSON.stringify(q.headers), body });
    if (box.requirePin && u.startsWith("/amble/cloud/") && q.headers["x-amble-pin"] !== "4321") { box.refused++; return json(s, 401, { ok: false, error: "pin required" }); }
    if (u === "/amble/cloud/login" && q.method === "POST") {
      let b = {}; try { b = JSON.parse(body); } catch { /* stays empty */ }
      if (b.email === "pin401@example.invalid") return json(s, 401, { ok: false, error: "pin required" });
      const o = b.saved ? (box.saved?.email_hint === "slow…@example.invalid" ? { state: "done", token: "slow@example.invalid", wait: 1500 } : { state: "done", token: "one@example.invalid" }) : outcomeFor(b.email);
      box.pending = { ...o, until: Date.now() + (o.wait || 0), remember: !!b.remember, polled: 0 };
      return json(s, 200, { ok: true, state: "queued" });
    }
    if (u === "/amble/cloud/login" && q.method === "GET") {
      const p = box.pending;
      if (!p) return json(s, 200, idleReply());
      if (p.polled++ === 0 || Date.now() < p.until) return json(s, 200, { ok: true, state: "busy" });
      box.pending = null;
      if (p.state === "done" && p.remember && !p.forgotten) box.op = { kind: "save", left: 1, hint: { email_hint: "r…@example.invalid", region: "us" } };
      const { until, wait, remember, polled, forgotten, ...out } = p;
      return json(s, 200, { ok: true, ...out });
    }
    if (u === "/amble/cloud/forget" && q.method === "POST") {
      if (box.pending) box.pending.forgotten = true;  // a sign-in still running will not be saved
      box.op = { kind: "forget", left: 1 };            // and a queued save is replaced by the erase
      return json(s, 200, { ok: true, state: "queued" });
    }
    if (q.method !== "GET") { s.statusCode = 405; return s.end(); }
    if (u === "/amble/status") return json(s, 200, status);
    if (u === "/amble/cloud.js") { s.setHeader("content-type", types[".js"]); return s.end(fakeCloud); }
    const f = u === "/amble/edit" ? "edit.html" : u.startsWith("/amble/") ? u.slice(7) : null;
    if (!f || f.includes("..") || !fs.existsSync(dist + "/" + f)) { s.statusCode = 404; return s.end(); }
    s.setHeader("content-type", types[path.extname(f)] || "application/octet-stream");
    s.end(fs.readFileSync(dist + "/" + f));
  });
}).listen(0, "127.0.0.1");
await new Promise((r) => srv.on("listening", r));
const base = `http://127.0.0.1:${srv.address().port}`;

let failed = 0;
const check = (c, m) => { console.log((c ? "ok:   " : "FAIL: ") + m); if (!c) failed++; };
const browser = await puppeteer.launch({ executablePath: "/Applications/Google Chrome.app/Contents/MacOS/Google Chrome", headless: true });
try {
  const page = await browser.newPage();
  page.on("pageerror", (e) => { console.log("pageerror:", e.message); failed++; });
  // Record the password field's value at the moment of every call to a box cloud route.
  await page.evaluateOnNewDocument(() => {
    window.__pwAtBox = [];
    window.__promptCalls = []; window.__promptReply = null;
    window.prompt = (m) => { window.__promptCalls.push(m); return window.__promptReply; };
    const real = window.fetch;
    window.fetch = function (url, ...rest) {
      if (String(url).startsWith("/amble/cloud/")) window.__pwAtBox.push(document.getElementById("acct-pw")?.value ?? "");
      return real.call(this, url, ...rest);
    };
  });
  const goto = async (saved = null) => {
    resetBox(saved);
    box.reqs.length = 0;
    await page.goto(base + "/amble/edit", { waitUntil: "networkidle0", timeout: 30000 });
    await page.waitForFunction(() => document.body.dataset.ready === "1", { timeout: 15000 });
  };
  // After a scenario: the field was empty at every box call, and the password crossed the wire once, in the login POST.
  const pwChecks = async (name) => {
    const at = await page.evaluate(() => { const a = window.__pwAtBox; window.__pwAtBox = []; return a; });
    check(at.every((v) => v === ""), `${name}: password field empty at every box request (${at.length})`);
    const hits = box.reqs.filter((r) => (r.url + r.headers + r.body).includes(PASSWORD));
    check(hits.length === 1 && hits[0].method === "POST" && hits[0].url === "/amble/cloud/login", `${name}: password in exactly one request, the login POST (${hits.length})`);
    box.reqs.length = 0;
  };
  const lastLoginPost = () => { const r = [...box.reqs].reverse().find((x) => x.method === "POST" && x.url === "/amble/cloud/login"); return r ? JSON.parse(r.body) : null; };
  const text = (sel) => page.$eval(sel, (e) => e.textContent);
  const settle = (sel) => page.waitForFunction((s) => !/^(Signing in on the box…|Fetching your mesh…)$/.test(document.querySelector(s).textContent), { timeout: 15000 }, sel);
  const enter = async (email) => {
    await page.$eval("#acct-email", (e) => { e.value = ""; });
    await page.type("#acct-email", email); await page.type("#acct-pw", PASSWORD);
  };
  const submit = () => page.evaluate(() => {
    document.getElementById("acct-fetch").click();
    return [document.getElementById("acct-pw").value, document.getElementById("acct-msg").textContent];
  });

  await goto();
  const init = await text("#msg");
  check(init === "", "fresh: page line empty");
  check(!(await page.$eval("#rows-empty", (e) => e.hidden)) && (await page.$eval("table", (e) => e.hidden)), "fresh: placeholder shown, table hidden");
  const follows = (a, b) => page.evaluate((x, y) => !!(document.querySelector(x).compareDocumentPosition(document.querySelector(y)) & Node.DOCUMENT_POSITION_FOLLOWING), a, b);
  check(await follows("#acct-form", "#acct-explain"), "fresh: account controls above the explanation");
  check(await follows("#file", "#door-db p.explain"), "fresh: database control above the explanation");
  check(!(await page.$("#acct-other")) && !(await page.$("#acct-done")), "fresh: no other-account link, no door done line");
  for (const id of ["door-account", "door-db", "door-takeover"]) check(!!(await page.$("#" + id)), `door present: ${id}`);
  check(JSON.stringify(await page.$$eval("#acct-region option", (o) => o.map((x) => x.value + (x.selected ? "*" : "")))) === '["us*","eu","cn"]', "regions us (selected), eu, cn");
  check(!(await page.$("#door-takeover button, #door-takeover input")), "take-over has no controls");
  check((await page.$eval("#acct-pw", (e) => e.type + "/" + e.autocomplete)) === "password/off", "password field: type password, autocomplete off");

  // Wrong password
  await enter("wrong@example.invalid");
  let [pw, msg] = await submit();
  check(pw === "", "wrong password: field cleared in the same tick");
  check(msg === "Signing in on the box…", "wrong password: message is Signing in on the box… in the same tick");
  await settle("#acct-msg");
  check(/did not accept that email and password/.test(await text("#acct-msg")), "wrong password: credentials copy");
  check((await page.$eval("#acct-msg", (e) => e.className)).includes("error"), "wrong password: error class");
  check(await page.$eval("#acct-fetch", (e) => !e.disabled), "wrong password: Fetch re-enabled");
  check((await text("#msg")) === init, "wrong password: page message untouched");
  await pwChecks("wrong password");

  // Offline
  await enter("offline@example.invalid");
  await submit(); await settle("#acct-msg");
  check(/could not reach amaran's servers/.test(await text("#acct-msg")), "offline: network copy");
  await pwChecks("offline");

  // Box says code 10001: the shape copy, not the credentials copy
  await enter("malformed@example.invalid");
  await submit(); await settle("#acct-msg");
  const mm = await text("#acct-msg");
  check(mm === "amaran refused the box's sign-in request (code 10001). The database import still works.", "malformed: login-request copy, not credentials");
  await pwChecks("malformed");

  // The box wants its PIN
  await enter("pin401@example.invalid");
  await submit(); await settle("#acct-msg");
  check(/asks for its PIN before it signs in\. Enter it and try again\./.test(await text("#acct-msg")), "pin401: PIN copy");
  check((await page.evaluate(() => window.__promptCalls.length)) === 1 && (await page.evaluate(() => sessionStorage.getItem("amble_pin"))) === null, "pin401: cancelled prompt shows the PIN copy, nothing stored");
  await page.evaluate(() => { window.__promptCalls = []; sessionStorage.removeItem("amble_pin"); });
  box.reqs.length = 0; await page.evaluate(() => { window.__pwAtBox = []; });

  // Two meshes
  await enter("two@example.invalid");
  await submit(); await settle("#acct-msg");
  const buttons = await page.$$("#acct-pick button[data-uuid]");
  check(buttons.length === 2, `two meshes: picker shows ${buttons.length} buttons`);
  check((await page.$$eval("#rows tr", (t) => t.length)) === 0, "two meshes: no rows before a choice");
  await buttons[0].click();
  await page.waitForFunction(() => document.querySelectorAll("#rows tr").length > 0, { timeout: 15000 });
  check((await page.$$eval("#rows tr", (t) => t.length)) >= 2, "two meshes: first choice loads rows");
  check((await page.$eval("#acct-pick", (e) => e.innerHTML)) === "", "two meshes: picker cleared after the choice");
  await pwChecks("two meshes");

  // One usable mesh among several: the picker still shows, the unusable one disabled
  await goto();
  await enter("onegood@example.invalid");
  await submit(); await settle("#acct-msg");
  const pb = await page.$$eval("#acct-pick button", (b) => b.map((x) => ({ d: x.disabled, t: x.textContent })));
  check(pb.length === 2, `onegood: picker shows ${pb.length} buttons`);
  check(pb.length === 2 && !pb[0].d && pb[1].d && /no fixtures/.test(pb[1].t), "onegood: second button disabled, labelled no fixtures");
  check((await page.$$eval("#rows tr", (t) => t.length)) === 0, "onegood: no rows before a choice");
  await (await page.$("#acct-pick button[data-uuid]")).click();
  await page.waitForFunction(() => document.querySelectorAll("#rows tr").length > 0, { timeout: 15000 });
  check((await page.$$eval("#rows tr", (t) => t.length)) >= 2, "onegood: clicking the first loads rows");
  await pwChecks("onegood");

  // Default fetch
  await goto();
  check(await page.$eval("#acct-remember", (e) => !e.checked), "remember: unchecked by default");
  check((await page.$eval("#acct-saved", (e) => e.hidden)) && (await page.$eval("#acct-form", (e) => !e.hidden)), "no saved sign-in: form shown, saved block hidden");
  await enter("one@example.invalid");
  await submit(); await page.waitForFunction(() => document.querySelectorAll("#rows tr").length > 0, { timeout: 15000 });
  const rows = await page.$$eval("#rows tr", (t) => t.map((tr) => ({ n: tr.children[1].textContent, c: !!tr.querySelector("input")?.checked })));
  const ticked = rows.filter((r) => r.c).length;
  console.log(`      rows: ${rows.length}, pre-ticked: ${ticked}`);
  check(rows.length === 3 && ticked === 1 && rows[0].c, "fetch: 3 rows (2 fetched + 1 gone), the configured fixture pre-ticked");
  const line = await text("#fixtures-msg");
  check(/^Fetched 2 fixture\(s\) from your amaran account\. Tick the ones this box should control, then Save\./.test(line), "fetch: fixtures line carries the result");
  check(/Since the box was last saved: 1 new, 1 no longer in your account/.test(line), "fetch: change line present");
  check(/3 records skipped: bad MAC ×1, duplicate MAC ×1, duplicate address ×1\./.test(line), "fetch: skipped line present, counts only (bad MAC, duplicate MAC, duplicate address)");
  check(!/Broken Record|Again|Five/.test(line), "fetch: skipped line names nothing");
  check(await page.$eval("#acct-form", (e) => !e.hidden), "fetch: form stays available");
  check(lastLoginPost()?.remember === false, "fetch: POST carries remember:false");
  check((await text("#acct-msg")) === "Signed in; nothing stored.", "fetch: door says signed in, nothing stored");
  check((await text("#msg")) === "", "fetch: page line stays empty, nothing said twice");
  check((await page.$eval("#rows-empty", (e) => e.hidden)) && !(await page.$eval("table", (e) => e.hidden)), "fetch: placeholder gone, table shown");
  await pwChecks("fetch");

  // Remember: opt-in, then the saved block appears
  await goto();
  await enter("remember@example.invalid");
  await page.click("#acct-remember");
  await submit(); await page.waitForFunction(() => document.querySelectorAll("#rows tr").length > 0, { timeout: 15000 });
  check(lastLoginPost()?.remember === true, "remember: POST carries remember:true");
  check((await text("#acct-msg")) === "Signed in. Your sign-in is saved on this box.", "remember: door says saved on this box");
  await page.waitForFunction(() => !document.getElementById("acct-saved").hidden, { timeout: 15000 });
  check((await text("#acct-saved-email")) === "r…@example.invalid" && (await text("#acct-saved-region")) === "United States", "remember: saved stack shows the hint and the region");
  check(await page.$eval("#acct-explain", (e) => e.hidden), "remember: explanation hidden once saved");
  await pwChecks("remember");
  await page.click("#acct-forget");
  await page.waitForFunction(() => document.getElementById("acct-saved").hidden, { timeout: 15000 });
  check((await page.$eval("#acct-form", (e) => !e.hidden)) && !(await page.$eval("#acct-explain", (e) => e.hidden)), "remember then Forget: form and explanation back, saved block hidden");

  // Saved sign-in at page load: Check, other account, Forget
  await goto(SAVED_HINT);
  await page.waitForFunction(() => !document.getElementById("acct-saved").hidden, { timeout: 15000 });
  check((await text("#acct-saved-email")) === "s…@example.invalid" && (await text("#acct-saved-region")) === "Europe", "saved: stack shows the hint and the region label");
  check((await page.$eval("#acct-form", (e) => e.hidden)) && (await page.$eval("#acct-explain", (e) => e.hidden)), "saved: form and explanation hidden");
  check((await text("#acct-refresh")) === "Refresh from amaran", "saved: the main action reads Refresh from amaran");
  await page.click("#acct-refresh");
  await page.waitForFunction(() => document.querySelectorAll("#rows tr").length > 0, { timeout: 15000 });
  const sp = lastLoginPost();
  check(sp && sp.saved === true && !("password" in sp) && !("email" in sp), "saved: Refresh from amaran posts {saved:true} only");
  check((await text("#acct-msg")) === "Signed in. Your sign-in is saved on this box.", "saved: door says saved on this box");
  const atBox = await page.evaluate(() => window.__pwAtBox);
  check(atBox.every((v) => v === ""), "saved: password field empty at every box request");
  check(!box.reqs.some((r) => (r.url + r.headers + r.body).includes(PASSWORD)), "saved: no request carries a password");
  await goto(SAVED_HINT);
  await page.waitForFunction(() => !document.getElementById("acct-saved").hidden, { timeout: 15000 });
  await page.click("#acct-forget");
  await page.waitForFunction(() => document.getElementById("acct-saved").hidden, { timeout: 15000 });
  check(box.reqs.some((r) => r.method === "POST" && r.url === "/amble/cloud/forget") && box.saved === null, "forget: POST sent, box holds nothing");
  check((await page.$eval("#acct-form", (e) => !e.hidden)) && (await text("#acct-msg")) === "Forgotten. The box no longer holds a sign-in.", "forget: form returns with the forgotten message");

  // Storage failures are said, not hidden
  await goto();
  box.fail.save = true;
  await enter("remember@example.invalid");
  await page.click("#acct-remember");
  await submit(); await page.waitForFunction(() => document.querySelectorAll("#rows tr").length > 0, { timeout: 15000 });
  check((await text("#acct-msg")) === "Signed in. The box could not save your sign-in. It was used for this fetch only.", "save failed: door says the box could not save");
  check(await page.$eval("#acct-saved", (e) => e.hidden), "save failed: no saved block");
  await goto(SAVED_HINT);
  box.fail.forget = true;
  await page.waitForFunction(() => !document.getElementById("acct-saved").hidden, { timeout: 15000 });
  await page.click("#acct-forget");
  await page.waitForFunction(() => /forget/.test(document.getElementById("acct-msg").textContent), { timeout: 15000 });
  check((await text("#acct-msg")) === "The box could not forget the sign-in. Try again.", "forget failed: says so");
  check(await page.$eval("#acct-saved", (e) => !e.hidden), "forget failed: saved block stays");

  // A reply without "store" confirms nothing, either way
  await goto();
  await enter("remember@example.invalid");
  await page.click("#acct-remember");
  box.omitStore = true;
  await submit(); await page.waitForFunction(() => document.querySelectorAll("#rows tr").length > 0, { timeout: 15000 });
  check((await text("#acct-msg")) === "Signed in. The box did not confirm it saved your sign-in.", "no store: remember is not confirmed");
  await goto(SAVED_HINT);
  box.omitStore = true;
  await page.waitForFunction(() => !document.getElementById("acct-saved").hidden, { timeout: 15000 });
  await page.click("#acct-forget");
  await page.waitForFunction(() => /confirm/.test(document.getElementById("acct-msg").textContent), { timeout: 15000 });
  check((await text("#acct-msg")) === "The box did not confirm it forgot the sign-in. Try again.", "no store: Forget is not confirmed even with saved:null");

  // Forget is off while a sign-in runs
  await goto({ email_hint: "slow…@example.invalid", region: "us" });
  await page.waitForFunction(() => !document.getElementById("acct-saved").hidden, { timeout: 15000 });
  await page.click("#acct-refresh");
  await page.waitForFunction(() => document.getElementById("acct-refresh").disabled, { timeout: 5000 });
  check(await page.$eval("#acct-forget", (e) => e.disabled), "in flight: Forget disabled while the sign-in runs");
  await page.waitForFunction(() => document.querySelectorAll("#rows tr").length > 0, { timeout: 15000 });
  check(await page.$eval("#acct-forget", (e) => !e.disabled), "in flight: Forget enabled again afterwards");

  // Saved refresh leaves the saved stack in place; Forget is the way to another account
  await goto(SAVED_HINT);
  await page.waitForFunction(() => !document.getElementById("acct-saved").hidden, { timeout: 15000 });
  await page.click("#acct-refresh");
  await page.waitForFunction(() => document.querySelectorAll("#rows tr").length > 0, { timeout: 15000 });
  check(!(await page.$eval("#acct-saved", (e) => e.hidden)) && (await page.$eval("#acct-form", (e) => e.hidden)), "refresh after load: saved stack stays, form stays hidden");

  // A box PIN: all three cloud routes must carry X-Amble-Pin
  await page.evaluate(() => sessionStorage.setItem("amble_pin", "4321"));
  box.requirePin = true; box.refused = 0;
  await goto(SAVED_HINT);
  await page.waitForFunction(() => !document.getElementById("acct-saved").hidden, { timeout: 15000 });
  await page.click("#acct-refresh");
  await page.waitForFunction(() => document.querySelectorAll("#rows tr").length > 0, { timeout: 15000 });
  await page.click("#acct-forget");
  await page.waitForFunction(() => document.getElementById("acct-saved").hidden, { timeout: 15000 });
  check(box.refused === 0, `PIN: saved GET, login POST, poll GET and forget POST all carried the PIN (${box.refused} refused)`);
  await page.evaluate(() => sessionStorage.removeItem("amble_pin"));
  box.requirePin = false;

  // A PIN box and no stored PIN: the account door asks once, stores it, and the retried sign-in succeeds
  await goto();
  await page.evaluate(() => { sessionStorage.removeItem("amble_pin"); window.__promptCalls = []; window.__promptReply = "4321"; });
  box.requirePin = true; box.refused = 0;
  await enter("one@example.invalid");
  await submit();
  await page.waitForFunction(() => document.querySelectorAll("#rows tr").length > 0, { timeout: 15000 });
  check((await page.evaluate(() => window.__promptCalls)).length === 1 && (await page.evaluate(() => window.__promptCalls[0])) === "This box has a PIN. Enter it:", "PIN prompt: asked exactly once, with the box's wording");
  check(box.refused === 1, `PIN prompt: only the first POST was refused (${box.refused})`);
  check((await page.evaluate(() => sessionStorage.getItem("amble_pin"))) === "4321", "PIN prompt: PIN stored under amble_pin");
  check(/^Fetched \d+ fixture\(s\) from your amaran account/.test(await text("#fixtures-msg")), "PIN prompt: the retried sign-in succeeded");
  await page.evaluate(() => { sessionStorage.removeItem("amble_pin"); window.__promptReply = null; window.__promptCalls = []; });
  box.requirePin = false; box.reqs.length = 0;

  // An unknown saved region is not sent to fetchMesh
  await goto({ email_hint: "s…@example.invalid", region: "zz" });
  await page.waitForFunction(() => !document.getElementById("acct-saved").hidden, { timeout: 15000 });
  box.reqs.length = 0;
  await page.click("#acct-refresh");
  check(/did not look like a mesh/.test(await text("#acct-msg")) && !box.reqs.some((r) => r.method === "POST"), "unknown saved region: shape copy, no sign-in attempted");

  // Database door
  const db = root + "/spikes/03-importer/amaran.db";
  if (fs.existsSync(db)) {
    await (await page.$("#file")).uploadFile(db);
    await page.waitForFunction(() => /fixture\(s\) in the database/.test(document.getElementById("fixtures-msg").textContent), { timeout: 15000 });
    check((await page.$$eval("#rows tr", (t) => t.length)) > 0, "database door: file loaded rows");
    check(!/your account/.test(await text("#fixtures-msg")), "database door: fixtures line does not say account");
    check((await text("#msg")) === "", "database door: page line stays empty");

    // A database chosen after an account fetch replaces the fixtures line; the door keeps its sign-in line.
    await goto();
    await enter("one@example.invalid");
    await submit(); await page.waitForFunction(() => document.querySelectorAll("#rows tr").length > 0, { timeout: 15000 });
    await (await page.$("#file")).uploadFile(db);
    await page.waitForFunction(() => /fixture\(s\) in the database/.test(document.getElementById("fixtures-msg").textContent), { timeout: 15000 });
    check(!/your account/.test(await text("#fixtures-msg")) && (await text("#acct-msg")) === "Signed in; nothing stored.", "database after account: fixtures line replaced, door line kept");
    await pwChecks("database after account");

    // A database chosen while the door asks which mesh: the prompt and the buttons go, nothing dangles.
    await goto();
    await enter("onegood@example.invalid");
    await submit(); await page.waitForFunction(() => document.querySelectorAll("#acct-pick button").length > 0, { timeout: 15000 });
    await (await page.$("#file")).uploadFile(db);
    await page.waitForFunction(() => /fixture\(s\) in the database/.test(document.getElementById("fixtures-msg").textContent), { timeout: 15000 });
    check((await text("#acct-msg")) === "" && (await page.$$eval("#acct-pick button", (b) => b.length)) === 0, "database over a mesh choice: prompt and buttons cleared");
    await pwChecks("database over a mesh choice");

    // A slow cloud fetch must not overwrite a database chosen while it was pending.
    await goto();
    await enter("slow@example.invalid");
    await submit();
    await (await page.$("#file")).uploadFile(db);
    await page.waitForFunction(() => /fixture\(s\) in the database/.test(document.getElementById("fixtures-msg").textContent), { timeout: 15000 });
    const dbRows = await page.$$eval("#rows tr", (t) => t.length), dbLine = await text("#fixtures-msg");
    await new Promise((r) => setTimeout(r, 2000));
    check((await page.$$eval("#rows tr", (t) => t.length)) === dbRows, `stale fetch: rows still the database's (${dbRows})`);
    check((await text("#fixtures-msg")) === dbLine && !/your account/.test(dbLine), "stale fetch: fixtures line still names the database");
    await pwChecks("slow");

    // A slow cloud fetch that FAILS after a database was chosen must stay silent.
    await goto();
    await enter("slowfail@example.invalid");
    await submit();
    await (await page.$("#file")).uploadFile(db);
    await page.waitForFunction(() => /fixture\(s\) in the database/.test(document.getElementById("fixtures-msg").textContent), { timeout: 15000 });
    const dbRows2 = await page.$$eval("#rows tr", (t) => t.length);
    await new Promise((r) => setTimeout(r, 2000));
    check((await text("#acct-msg")) === "", "stale failure: account message still empty");
    check((await page.$$eval("#rows tr", (t) => t.length)) === dbRows2 && dbRows2 > 0, `stale failure: rows still the database's (${dbRows2})`);
    await pwChecks("slowfail");
  } else console.log("skip: database door (spikes/03-importer/amaran.db not present)");
} finally { await browser.close(); srv.close(); }
console.log(failed ? `\n${failed} check(s) FAILED` : "\nall checks passed");
process.exit(failed ? 1 : 0);
