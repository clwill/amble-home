import test from "node:test";
import assert from "node:assert/strict";
import { boxName } from "../importer/importer.js";
import { rowsFor, selectedPayload, renderStatus, escapeHtml, idFor, describeImport, lockedText, rowHtml, importStep, pinOutcome, SAVE_DEADLINE_MS, swatchFor, kelvinToRgb, lampHtml, meshLine, applyName, changeLine, skippedLine } from "./box.js";

const extracted = { fixtures: [
  { mac: "AA:BB:CC:8D:ED:B3", address: 0x1E, name: "amaran P60c", caps: {} },
  { mac: "AA:BB:CC:00:00:01", address: 0x0C, name: 'Key "left"', caps: {} },
] };
const status = { configured: true, fixtures: [{ id: "amble_8dedb3", name: "amaran P60c", live: true, on: false, brightness: 200, mode: "cct", kelvin: 3200 }] };

test("idFor matches the firmware's object id", () => { assert.equal(idFor("AA:BB:CC:8D:ED:B3"), "amble_8dedb3"); });

test("rows_with_no_active_config", () => {
  const rows = rowsFor(extracted, { configured: false, fixtures: [] });
  assert.equal(rows.length, 2);
  assert.ok(rows.every((r) => !r.checked && !r.configured && r.live === null));
  assert.equal(rowsFor(extracted, null).length, 2);
});

test("rows_prechecked_by_mac", () => {
  const rows = rowsFor(extracted, status);
  assert.deepEqual(rows.map((r) => r.checked), [true, false]);
  assert.equal(rows[0].live, true); assert.equal(rows[0].hex, "0x001E");
});

test("rows_escape_names", () => {
  assert.equal(escapeHtml('Key "left" <b>&'), "Key &quot;left&quot; &lt;b&gt;&amp;");
  assert.equal(escapeHtml(undefined), "");
});

test("payload_contains_only_checked", () => {
  const rows = rowsFor(extracted, status);
  const calls = [];
  const fake = (result, opts) => { calls.push(opts); return { fixtures: opts.selected }; };
  const p = selectedPayload({ keys: {} }, rows, fake);
  assert.deepEqual(p.fixtures, ["AA:BB:CC:8D:ED:B3"]);
  rows[1].checked = true;
  assert.equal(selectedPayload({}, rows, fake).fixtures.length, 2);
});

test("status_summary_cct", () => {
  const s = { configured: true, fixtures: [{ id: "a", name: "L", live: true, heard: true, on: true, brightness: 600, mode: "cct", kelvin: 3200 }] };
  const [r] = renderStatus(s);
  assert.deepEqual({ ...r, swatch: undefined }, { id: "a", name: "L", state: "live", summary: "on · 60 % · 3200 K", swatch: undefined });
  assert.equal(r.swatch.glow, 0.6);
});

test("status_summary_hsi_and_off", () => {
  const s = { configured: true, fixtures: [
    { id: "a", name: "L", live: false, heard: true, on: true, brightness: 200, mode: "hsi", hue: 120, sat: 50 },
    { id: "b", name: "M", live: true, heard: true, on: false, brightness: 200, mode: "cct", kelvin: 5600 },
  ] };
  const r = renderStatus(s);
  assert.equal(r[0].state, "dark"); assert.equal(r[0].summary, "on · 20 % · hue 120° sat 50 %");
  assert.equal(r[1].summary, "off");
});

test("status_na_when_never_heard", () => {
  const s = { configured: true, fixtures: [{ id: "a", name: "L", live: false, heard: false, on: false }] };
  assert.deepEqual(renderStatus(s)[0], { id: "a", name: "L", state: "na", summary: "<NA>", swatch: null });
  assert.deepEqual(renderStatus({ configured: false }), []);
});

test("describeImport states", () => {
  assert.equal(describeImport({ state: "idle" }), "");
  assert.match(describeImport({ state: "committed" }), /rebooting/);
  assert.equal(describeImport({ state: "failed", error: "x" }), "Save failed: x");
});

test("lockedText rounds up the wait", () => {
  assert.equal(lockedText({ retry_ms: 29924 }), "Locked after too many wrong PINs; try again in 30 s");
  assert.equal(lockedText({}), "Locked after too many wrong PINs; try again in 30 s");
});

test("rows_reject_malformed_mac (D1): no id, cannot be ticked, nothing unescaped reaches the row", () => {
  const evil = { fixtures: [{ mac: '"><img src=x onerror=alert(1)>', address: 40, name: "<b>Evil</b>", caps: {} }] };
  assert.equal(idFor(evil.fixtures[0].mac), null);
  const rows = rowsFor(evil, status);
  assert.equal(rows[0].invalid, true); assert.equal(rows[0].id, null); assert.equal(rows[0].checked, false);
  const html = rowHtml(rows[0], 0);
  assert.ok(!html.includes("<img"), "markup from the database must not survive");
  assert.ok(!html.includes("<b>Evil"), "names are escaped");
  assert.ok(html.includes("disabled"));
  assert.ok(!html.includes("data-id="));
  const good = rowHtml(rowsFor(extracted, status)[0], 0);
  assert.ok(good.includes('data-id="amble_8dedb3"'));
});

test("importStep (D2): committed, missed window, box down, failed, deadline", () => {
  const t0 = 1000;
  const s0 = { phase: "saving", since: t0, message: "Saving…" };
  assert.equal(importStep(s0, { ok: true, status: { import: { state: "queued" } } }, t0 + 500).phase, "saving");
  assert.equal(importStep(s0, { ok: true, status: { import: { state: "committed" } } }, t0 + 1000).phase, "rebooting");
  assert.equal(importStep(s0, { ok: false }, t0 + 2000).phase, "rebooting");                 // missed the window, box down
  assert.equal(importStep(s0, { ok: true, status: { import: { state: "idle" }, mesh: {} } }, t0 + 9000).phase, "done");  // missed it entirely
  const f = importStep(s0, { ok: true, status: { import: { state: "failed", error: "x" } } }, t0 + 1000);
  assert.equal(f.phase, "failed"); assert.match(f.message, /x/);
  const r = { phase: "rebooting", since: t0 };
  assert.equal(importStep(r, { ok: false }, t0 + 5000).phase, "rebooting");
  assert.equal(importStep(r, { ok: true, status: { mesh: { up: false }, import: { state: "idle" } } }, t0 + 9000).phase, "done");
  // D2 targeted: a dropped fetch followed by a reply that says the import failed is a failure, not success
  const dropped = importStep(s0, { ok: false }, t0 + 2000);
  const after = importStep(dropped, { ok: true, status: { mesh: {}, import: { state: "failed", error: "could not store the configuration" } } }, t0 + 4000);
  assert.equal(after.phase, "failed"); assert.match(after.message, /could not store/);
  assert.equal(importStep(dropped, { ok: true, status: { mesh: {}, import: { state: "committed" } } }, t0 + 4000).phase, "rebooting");
  assert.equal(importStep(dropped, { ok: true, status: { mesh: {}, import: { state: "queued" } } }, t0 + 4000).phase, "rebooting");
  assert.equal(importStep(r, { ok: false }, t0 + SAVE_DEADLINE_MS + 1).phase, "unknown");
  assert.equal(importStep({ phase: "done", since: t0 }, { ok: false }, t0 + SAVE_DEADLINE_MS + 1).phase, "done");
});

test("pinOutcome (D3): pending, stored, storage failure, mismatch", () => {
  assert.deepEqual(pinOutcome({ pin_pending: true }, true), { done: false });
  assert.deepEqual(pinOutcome({ pin_pending: false, pin_set: true, pin_error: "" }, true), { done: true, ok: true, message: "PIN set." });
  assert.deepEqual(pinOutcome({ pin_pending: false, pin_set: false, pin_error: "" }, false), { done: true, ok: true, message: "PIN removed." });
  assert.equal(pinOutcome({ pin_pending: false, pin_set: false, pin_error: "could not store the PIN" }, true).ok, false);
  assert.equal(pinOutcome({ pin_pending: false, pin_set: false, pin_error: "" }, true).ok, false);
});

test("swatch: CCT warm is redder than cool, HSI keeps its hue, off and unheard have none", () => {
  const [wr, , wb] = kelvinToRgb(2700), [cr, , cb] = kelvinToRgb(7500);
  assert.ok(wr > wb && cb >= cr, "2700 K leans red, 7500 K leans blue");
  const cct = swatchFor({ on: true, brightness: 600, mode: "cct", kelvin: 3200 });
  assert.match(cct.color, /^rgb\(\d+ \d+ \d+\)$/); assert.equal(cct.glow, 0.6);
  const hsi = swatchFor({ on: true, brightness: 100, mode: "hsi", hue: 120, sat: 50 });
  assert.match(hsi.color, /^hsl\(120 50% \d+%\)$/); assert.ok(hsi.glow >= 0.15);
  assert.equal(swatchFor({ on: false, brightness: 500, mode: "cct", kelvin: 5600 }), null);
  assert.equal(swatchFor({ heard: false }), null);
});
test("lamp html carries only numbers and our own colour string, never user text", () => {
  const s = { configured: true, fixtures: [{ id: "a", name: "<x>", heard: true, live: true, on: true, brightness: 1000, mode: "cct", kelvin: 5600 }, { id: "b", name: "L", heard: false }] };
  const [onLine, naLine] = renderStatus(s);
  assert.match(lampHtml(onLine), /^<span class="lamp" style="--c:rgb\(\d+ \d+ \d+\);--g:1\.00" aria-label="on"><\/span>$/);
  assert.equal(lampHtml(naLine), '<span class="lamp na" aria-label="never heard"></span>');
  assert.equal(lampHtml({ state: "dark", swatch: null }), '<span class="lamp off" aria-label="off"></span>');
});

test("meshLine names the box's own node; applyName takes only a hostname", () => {
  assert.equal(meshLine({ mesh: { up: true, own: 0x72c3 } }), "mesh up · this box is node 0x72C3");
  assert.equal(meshLine({ mesh: { up: true } }), "mesh up");
  assert.equal(meshLine({ mesh: { up: false, error: "x" } }), "mesh down: x");
  assert.equal(meshLine({}), "mesh starting");
  const made = []; const doc = { title: "", getElementById: () => ({ set textContent(v) { made.push(v); } }) };
  assert.equal(applyName(doc, "amble-home-a1b2c3", "Edit"), true);
  assert.deepEqual(made, ["amble-home-a1b2c3"]); assert.equal(doc.title, "amble-home-a1b2c3 · Edit");
  assert.equal(applyName(doc, "<b>x</b>"), false); assert.equal(applyName(doc, undefined), false);
});

const st2 = { configured: true, fixtures: [
  { id: "amble_8dedb3", name: "amaran P60c", live: true },
  { id: "amble_000009", name: "Old lamp", live: false },
] };
const ex2 = { fixtures: [
  { mac: "AA:BB:CC:8D:ED:B3", address: 0x1E, name: "P60c bench", caps: {} },  // renamed
  { mac: "AA:BB:CC:00:00:01", address: 0x0C, name: "New one", caps: {} },     // new
] };

test("rows_change_states", () => {
  const rows = rowsFor(ex2, st2);
  assert.deepEqual(rows.map((r) => [r.change, r.checked]), [["renamed", true], ["new", false], ["gone", false]]);
  assert.equal(rows[0].was, "amaran P60c");
  assert.equal(rows[2].name, "Old lamp"); assert.equal(rows[2].id, "amble_000009"); assert.ok(rows[2].invalid && rows[2].gone);
  assert.ok(rowsFor(ex2, { configured: false, fixtures: [] }).every((r) => r.change === null));
  assert.equal(rowsFor(extracted, status)[0].change, "same");
});

test("change_line_copy", () => {
  assert.equal(changeLine(rowsFor(ex2, st2)), "Since the box was last saved: 1 new, 1 renamed, 1 no longer in your account (Save removes it from this box).");
  const goneTwo = { configured: true, fixtures: [{ id: "amble_000009", name: "A" }, { id: "amble_00000a", name: "B" }] };
  assert.equal(changeLine(rowsFor({ fixtures: [] }, goneTwo)), "Since the box was last saved: 2 no longer in your account (Save removes them from this box).");
  assert.equal(changeLine(rowsFor({ fixtures: [extracted.fixtures[0]] }, status)), "");
  assert.equal(changeLine(rowsFor(ex2, { configured: false })), "");
});

test("row_html_change_marks", () => {
  const rows = rowsFor(ex2, st2);
  assert.match(rowHtml(rows[0], 0), /was “amaran P60c”/);
  assert.match(rowHtml(rows[1], 1), /· new/);
  const gone = rowHtml(rows[2], 2);
  assert.match(gone, /no longer in your account/); assert.match(gone, /<input type="checkbox" data-i="2"  disabled>/); assert.match(gone, /data-id="amble_000009"/);
  assert.doesNotMatch(rowHtml(rowsFor(extracted, status)[0], 0), /class="muted change"/);
  const evil = rowHtml({ ...rows[0], was: '<b>"x"' }, 0);
  assert.match(evil, /was “&lt;b&gt;&quot;x&quot;”/);
});

test("skippedLine: counts and reasons only", () => {
  assert.equal(skippedLine([]), ""); assert.equal(skippedLine(undefined), "");
  assert.equal(skippedLine([{ name: "Secret", reason: "bad MAC" }]), "1 record skipped: bad MAC ×1.");
  const two = skippedLine([{ name: "A", reason: "bad MAC" }, { name: "B", reason: "no mesh" }]);
  assert.equal(two, "2 records skipped: bad MAC ×1, no mesh ×1.");
  assert.equal(skippedLine([{ reason: "bad MAC" }, { reason: "bad MAC" }, { reason: "no mesh" }]), "3 records skipped: bad MAC ×2, no mesh ×1.");
  assert.doesNotMatch(two, /[AB]\b.*name/);
});

test("a status name equal to the truncated long name is not renamed (I2)", () => {
  const long = "灯".repeat(11);
  const st = { configured: true, fixtures: [{ id: "amble_000001", name: "灯".repeat(10), live: true }] };
  const rows = rowsFor({ fixtures: [{ mac: "AA:BB:CC:00:00:01", address: 3, name: boxName(long) }] }, st);
  assert.equal(rows[0].change, "same");
});

test("an invalid-MAC fetched row has no change class (M6)", () => {
  const rows = rowsFor({ fixtures: [{ mac: "nonsense", address: 3, name: "X" }] }, st2);
  assert.equal(rows[0].change, null);
  assert.equal(changeLine(rows.filter((r) => !r.gone)), "");
});

test("source noun reaches the change line and the gone mark (M4)", () => {
  const rows = rowsFor(ex2, st2, "database");
  assert.match(changeLine(rows, "database"), /1 no longer in the database \(Save removes it/);
  assert.doesNotMatch(changeLine(rows, "database"), /account/);
  assert.match(rowHtml(rows[2], 2), /no longer in the database/);
  assert.match(rowHtml(rowsFor(ex2, st2)[2], 2), /no longer in your account/);
  assert.match(changeLine(rowsFor(ex2, st2)), /no longer in your account/);
});
