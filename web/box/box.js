// amble-home's two pages, Home (status, read-only) and Edit (import, select,
// identify, save), as pure helpers plus a small amount of DOM wiring at the
// bottom. The helpers have no imports and are tested with `node --test`.
// Served from the box at /amble/box.js; see web/box/build.mjs.

export const escapeHtml = (s) =>
  String(s ?? "").replace(/[&<>"']/g, (c) => ({ "&": "&amp;", "<": "&lt;", ">": "&gt;", '"': "&quot;", "'": "&#39;" }[c]));

export const hex4 = (n) => "0x" + Number(n).toString(16).toUpperCase().padStart(4, "0");

export const MAC_RE = /^([0-9A-F]{2}:){5}[0-9A-F]{2}$/;

// The box's stable id for a fixture: "amble_" + the last three MAC bytes
// (fixture.cpp object_id_for). null for anything that is not a well-formed MAC:
// the id becomes an HTML attribute, so only hex may ever reach it.
export function idFor(mac) {
  const m = String(mac).toUpperCase();
  if (!MAC_RE.test(m)) return null;
  return "amble_" + m.split(":").slice(3).join("").toLowerCase();
}

// Edit-page rows: every fixture the database offers, marked configured/live from
// the box's status. Nothing is pre-checked on a box with no configuration.
export function rowsFor(extracted, status, source = "account") {
  const known = !!status?.configured && Array.isArray(status.fixtures);
  const byId = new Map();
  if (known) for (const f of status.fixtures) byId.set(f.id, f);
  const seen = new Set();
  const rows = (extracted?.fixtures ?? []).map((f) => {
    const id = idFor(f.mac);
    const cfg = id ? byId.get(id) : undefined;
    if (cfg) seen.add(id);
    let change = null, was;
    if (known) {
      if (id === null) change = null;  // unusable entry: not a change, it cannot be saved
      else if (!cfg) change = "new";
      else if (cfg.name !== f.name) { change = "renamed"; was = cfg.name; }
      else change = "same";
    }
    return {
      id,
      invalid: id === null,
      mac: f.mac,
      address: Number(f.address),
      hex: hex4(f.address),
      name: f.name,
      configured: !!cfg,
      live: cfg ? !!cfg.live : null,
      checked: !!cfg,
      change,
      was,
    };
  });
  // Fixtures the box has that the fetched list lacks: shown, never selectable.
  if (known) {
    for (const f of status.fixtures) {
      if (seen.has(f.id)) continue;
      rows.push({ id: f.id, invalid: true, gone: true, mac: null, address: null, hex: "", name: f.name,
        configured: true, live: !!f.live, checked: false, change: "gone", source });
    }
  }
  return rows;
}

// Edit-page rows before any fetch: the fixtures the box already holds, from the same
// status call the Home page uses. ID works on them at once (the moment it is wanted is
// right after a Save, when the box has just rebooted and the fetched list is gone).
// They cannot be checked: Save needs keys and addresses that the status call does not
// carry, so the selection changes only after a fetch replaces these rows.
export function savedRows(status) {
  if (!status?.configured || !Array.isArray(status.fixtures)) return [];
  return status.fixtures.map((f) => ({
    id: f.id, invalid: false, saved: true, mac: null, address: null, hex: "", name: f.name,
    configured: true, live: !!f.live, checked: false, change: null,
  }));
}

const sourcePhrase = (source) => (source === "database" ? "the database" : "your account");

// One sentence on what differs from the box's saved fixtures; "" when nothing does.
export function changeLine(rows, source = "account") {
  const n = (c) => rows.filter((r) => r.change === c).length;
  const nw = n("new"), rn = n("renamed"), gone = n("gone");
  const parts = [];
  if (nw) parts.push(`${nw} new`);
  if (rn) parts.push(`${rn} renamed`);
  if (gone) parts.push(`${gone} no longer in ${sourcePhrase(source)} (Save removes ${gone === 1 ? "it" : "them"} from this box)`);
  return parts.length ? "Since the box was last saved: " + parts.join(", ") + "." : "";
}

// One sentence on records the importer left out: counts by reason, never names.
export function skippedLine(skipped) {
  if (!skipped?.length) return "";
  const by = new Map();
  for (const s of skipped) by.set(s.reason, (by.get(s.reason) ?? 0) + 1);
  const n = skipped.length;
  return `${n} ${n === 1 ? "record" : "records"} skipped: ${[...by].map(([r, c]) => `${r} ×${c}`).join(", ")}.`;
}

// One Edit-table row. Every value is escaped; an invalid row cannot be checked or flashed;
// a saved row (before a fetch) can be flashed but not checked.
export function rowHtml(r, i) {
  const dis = r.invalid || r.saved ? "disabled" : "";
  const dot = r.configured ? `<span class="lamp live-dot ${r.live ? "" : "off"}" style="--c:var(--live);--g:1" aria-label="${r.live ? "answering" : "configured, silent"}"></span>` : "";
  const idBtn = r.gone || !(r.invalid || !r.configured)
    ? `<button class="small" data-id="${escapeHtml(r.id)}">ID</button>`
    : `<button class="small" disabled title="${r.invalid ? "unusable entry" : "Save first; ID works on configured fixtures"}">ID</button>`;
  const mark = r.change === "new" ? " · new"
    : r.change === "renamed" ? ` · was “${escapeHtml(r.was)}”`
    : r.change === "gone" ? ` · no longer in ${sourcePhrase(r.source)}` : "";
  const markHtml = mark ? `<span class="muted change">${mark}</span>` : "";
  return `<tr${r.invalid ? ' class="muted"' : ""}><td><input type="checkbox" data-i="${Number(i)}" ${r.checked ? "checked" : ""} ${dis}></td>` +
    `<td>${escapeHtml(r.name)}${markHtml}</td><td><code>${escapeHtml(r.hex)}</code></td><td>${dot}</td><td>${idBtn}</td></tr>`;
}

// Save progress as a pure step function, so missing the box's brief "committed"
// window (it reboots 1.5 s later) can never strand the page. `sample` is
// {ok:true, status} for a successful GET /amble/status, {ok:false} otherwise.
export const SAVE_DEADLINE_MS = 90000;
export const STATUS_TIMEOUT_MS = 2500;
export function importStep(state, sample, now) {
  const since = state.since ?? now;
  if (now - since > SAVE_DEADLINE_MS && state.phase !== "done" && state.phase !== "failed") {
    return { phase: "unknown", since, message: "Could not confirm the save. Open Home to check which fixtures the box has." };
  }
  const imp = sample.ok ? sample.status?.import : undefined;
  switch (state.phase) {
    case "saving":
      if (!sample.ok) return { phase: "rebooting", since, message: "Saved. The box is rebooting to build the new entities…" };
      if (imp?.state === "failed") return { phase: "failed", since, message: "Save failed: " + (imp.error || "unknown error") };
      if (imp?.state === "committed") return { phase: "rebooting", since, message: "Saved. The box is rebooting to build the new entities…" };
      if (imp?.state === "idle") return { phase: "done", since, message: "Saved. The box is back." };  // we missed the window; it already rebooted
      return { ...state, since, message: "Saving…" };
    case "rebooting":
      // Reconnecting is not success: the box must say the import is over. A reboot
      // resets import.state to idle; queued/committed mean it has not happened yet.
      if (!sample.ok) return state;
      if (imp?.state === "failed") return { phase: "failed", since, message: "Save failed: " + (imp.error || "unknown error") };
      if (imp?.state === "idle") return { phase: "done", since, message: "Saved. The box is back." };
      return state;
    default:
      return state;
  }
}

// Outcome of a PIN change, from GET /amble/status after POST /amble/pin was queued.
export function pinOutcome(status, wantSet) {
  if (!status || status.pin_pending) return { done: false };
  if (status.pin_error) return { done: true, ok: false, message: "PIN not changed: " + status.pin_error };
  if (!!status.pin_set !== wantSet) return { done: true, ok: false, message: "PIN not changed." };
  return { done: true, ok: true, message: wantSet ? "PIN set." : "PIN removed." };
}

// What Save sends: the importer's payload for the checked rows only.
export function selectedPayload(result, rows, toImportPayload) {
  const selected = rows.filter((r) => r.checked).map((r) => r.mac);
  return toImportPayload(result, { selected });
}

// The color a fixture is showing, as CSS, for the lamp swatch on the Home page.
// CCT is mapped through a small white-point curve (2000 K amber to 10000 K blue
// white); HSI through hsl(). Brightness sets the glow, not the hue, so a dim red
// still reads as red. Returns null when the fixture is off or never heard.
export function kelvinToRgb(k) {
  const t = Math.min(10000, Math.max(1500, Number(k) || 5600)) / 100;
  let r, g, b;
  if (t <= 66) { r = 255; g = 99.47 * Math.log(t) - 161.12; b = t <= 19 ? 0 : 138.52 * Math.log(t - 10) - 305.04; }
  else { r = 329.7 * Math.pow(t - 60, -0.1332); g = 288.12 * Math.pow(t - 60, -0.0755); b = 255; }
  const c = (v) => Math.round(Math.min(255, Math.max(0, v)));
  return [c(r), c(g), c(b)];
}
export function swatchFor(f) {
  const heard = f.heard === undefined ? "on" in f : !!f.heard;
  if (!heard || !f.on) return null;
  const glow = Math.min(1, Math.max(0.15, (f.brightness ?? 0) / 1000));
  if (f.mode === "hsi") return { color: `hsl(${Math.round(f.hue ?? 0)} ${Math.round(f.sat ?? 100)}% ${Math.round(45 + 15 * glow)}%)`, glow };
  const [r, g, b] = kelvinToRgb(f.kelvin);
  return { color: `rgb(${r} ${g} ${b})`, glow };
}

// Home-page lines from GET /amble/status. Three states (spec "The Home page"):
// live (answering), dark (configured, silent for a while), na (never heard).
export function renderStatus(status) {
  if (!status?.configured) return [];
  return (status.fixtures ?? []).map((f) => {
    const heard = f.heard === undefined ? "on" in f : !!f.heard;
    const state = f.live ? "live" : heard ? "dark" : "na";
    let summary = "<NA>";
    if (heard) {
      if (!f.on) summary = "off";
      else {
        const pct = Math.round((f.brightness ?? 0) / 10) + " %";
        summary = f.mode === "hsi" ? `on · ${pct} · hue ${f.hue ?? "?"}° sat ${f.sat ?? "?"} %` : `on · ${pct} · ${f.kelvin} K`;
      }
    }
    return { id: f.id, name: f.name, state, summary, swatch: swatchFor(f) };
  });
}
// The lamp cell for a Home-page line: color as a CSS custom property (a number
// and a CSS color we built ourselves; never user text), state as a class.
export function lampHtml(line) {
  if (line.state === "na") return `<span class="lamp na" aria-label="never heard"></span>`;
  if (!line.swatch) return `<span class="lamp off" aria-label="off"></span>`;
  return `<span class="lamp" style="--c:${line.swatch.color};--g:${line.swatch.glow.toFixed(2)}" aria-label="on"></span>`;
}

// The Home page's one-line mesh summary. The box's own node address is shown so
// two boxes can be told apart on the mesh as well as by name.
export function meshLine(s) {
  if (!s?.mesh?.up) return s?.mesh?.error ? "mesh down: " + s.mesh.error : "mesh starting";
  return s.mesh.own ? `mesh up · this box is node ${hex4(s.mesh.own)}` : "mesh up";
}
// The box's real name (amble-home-xxxxxx) into the wordmark and the tab title.
export function applyName(doc, name, page) {
  if (typeof name !== "string" || !/^[a-z0-9-]{1,63}$/.test(name)) return false;
  const el = doc.getElementById("brand-name");
  if (el) el.textContent = name;
  doc.title = page ? `${name} · ${page}` : name;
  return true;
}
export const lockedText = (j) => `Locked after too many wrong PINs; try again in ${Math.ceil((j?.retry_ms ?? 30000) / 1000)} s`;

export function describeImport(imp) {
  switch (imp?.state) {
    case "queued": return "Saving…";
    case "committed": return "Saved. The box is rebooting to build the new entities…";
    case "failed": return "Save failed: " + (imp.error || "unknown error");
    default: return "";
  }
}

// ---- DOM wiring (skipped under node) ------------------------------------------------
if (typeof document !== "undefined") {
  const page = document.body.dataset.page;
  const $ = (id) => document.getElementById(id);
  const pinKey = "amble_pin";
  const headers = () => {
    const pin = sessionStorage.getItem(pinKey);
    return pin ? { "X-Amble-Pin": pin } : {};
  };
  // One retry with a freshly prompted PIN on 401; a 429 is reported, not retried.
  async function post(path, body) {
    for (let attempt = 0; attempt < 2; attempt++) {
      const res = await fetch(path, { method: "POST", headers: { "Content-Type": "application/json", ...headers() }, body: JSON.stringify(body) });
      let j = {};
      try { j = await res.json(); } catch { /* fixed text below */ }
      if (res.status === 401 && j.error === "locked") return { ok: false, error: lockedText(j) };
      if (res.status === 401 && attempt === 0) {
        const pin = prompt("This box has a PIN. Enter it:");
        if (!pin) return { ok: false, error: "PIN required" };
        sessionStorage.setItem(pinKey, pin);
        continue;
      }
      if (!res.ok) return { ok: false, error: j.error || `HTTP ${res.status}` };
      return j;
    }
    return { ok: false, error: "PIN wrong" };
  }
  // Every status request gives up after STATUS_TIMEOUT_MS. While the box reboots a
  // request to it can hang rather than fail; a few of those would fill the
  // browser's per-host connection limit and queue every later poll behind them.
  // Aborting frees the slot, so a reboot can never stall the save poll.
  const getStatus = async () => {
    const ctl = new AbortController();
    const t = setTimeout(() => ctl.abort(), STATUS_TIMEOUT_MS);
    try { return await (await fetch("/amble/status", { cache: "no-store", signal: ctl.signal })).json(); }
    finally { clearTimeout(t); }
  };

  if (page === "home") {
    const tick = async () => {
      try {
        const s = await getStatus();
        const lines = renderStatus(s);
        $("mesh").textContent = meshLine(s);
        applyName(document, s.name);
        $("rows").innerHTML = s.configured
          ? lines.map((l) => `<tr class="${l.state}"><td class="lamp-cell">${lampHtml(l)}</td><td>${escapeHtml(l.name)}</td><td class="summary muted">${escapeHtml(l.summary)}</td></tr>`).join("")
          : `<tr><td colspan="3" class="muted">No fixtures yet. <a href="/amble/edit">Import them from your amaran account or the app's database.</a></td></tr>`;
      } catch { $("mesh").textContent = "box unreachable"; }
    };
    tick(); setInterval(tick, 5000);
  }

  if (page === "edit") {
    window.ambleEdit = async ({ extract, toImportPayload, caps, SQL, cloud }) => {
      let status = null, result = null, rows = [];
      try { status = await getStatus(); applyName(document, status.name, "Edit"); } catch { /* table still works without the box's view */ }
      $("pin").textContent = status?.pin_set ? "PIN set" : "no PIN";
      const draw = () => {
        $("rows").innerHTML = rows.map(rowHtml).join("");
        $("save").disabled = !rows.some((r) => r.checked);
        $("rows-empty").hidden = rows.length > 0;
        document.querySelector("table").hidden = rows.length === 0;
      };
      // The fetch result and the change summary live under "Your fixtures"; #msg is for Save and the PIN.
      const fixturesLine = (t) => { $("fixtures-msg").textContent = t; $("fixtures-msg").hidden = !t; };
      let gen = 0;  // newest source selection wins: a stale completion sets nothing
      const load = (res, line, source) => {
        result = res; rows = rowsFor(result, status, source); draw();
        fixturesLine([line, skippedLine(res.skipped), changeLine(rows, source)].filter(Boolean).join(" "));
      };
      // On open: what the box holds, with ID live. A fetch replaces these rows.
      rows = savedRows(status); draw();
      if (rows.length) fixturesLine("The fixtures on this box. ID flashes one. To change the selection, fetch from your account or open the database.");
      $("file").addEventListener("change", async (ev) => {
        const file = ev.target.files[0]; if (!file) return;
        const my = ++gen;
        if ($("acct-pick")) {
          if ($("acct-pick").innerHTML) $("acct-msg").textContent = "";  // the door was asking which mesh: the file answers that
          $("acct-pick").innerHTML = ""; $("acct-pick").onclick = null;
        }
        fixturesLine("Reading…");
        let db;
        try {
          db = new SQL.Database(new Uint8Array(await file.arrayBuffer()));
          const res = extract(db, caps, { withKeys: true });
          if (my !== gen) return;
          load(res, `${res.fixtures.length} fixture(s) in the database. Check the ones this box should control, then Save.`, "database");
        } catch (e) {
          if (my !== gen) return;
          fixturesLine(e?.name === "ImportError" || e?.constructor?.name === "ImportError" ? e.message : "Could not open this file as an amaran database.");
        } finally { db?.close(); }
      });
      $("rows").addEventListener("change", (ev) => { const i = ev.target.dataset.i; if (i !== undefined) { rows[i].checked = ev.target.checked; $("save").disabled = !rows.some((r) => r.checked); } });
      $("rows").addEventListener("click", async (ev) => {
        const id = ev.target.dataset.id; if (!id) return;
        const r = await post("/amble/identify", { id });
        $("msg").textContent = r.ok ? "Flashing…" : r.error;
      });
      $("save").addEventListener("click", async () => {
        let payload;
        try { payload = selectedPayload(result, rows, toImportPayload); } catch (e) { $("msg").textContent = e.message; return; }
        $("save").disabled = true;
        const r = await post("/amble/import", payload);
        if (!r.ok) { $("msg").textContent = r.error; $("save").disabled = false; return; }
        let st = { phase: "saving", since: Date.now(), message: "Saving…" };
        $("msg").textContent = st.message;
        let inFlight = false;  // one poll at a time: a slow request must not stack up behind itself
        const poll = setInterval(async () => {
          if (inFlight) return;
          inFlight = true;
          let sample;
          try { sample = { ok: true, status: await getStatus() }; } catch { sample = { ok: false }; }
          finally { inFlight = false; }
          st = importStep(st, sample, Date.now());
          $("msg").textContent = st.message;
          if (st.phase === "done") { clearInterval(poll); setTimeout(() => (location.href = "/"), 2000); }
          if (st.phase === "failed" || st.phase === "unknown") { clearInterval(poll); $("save").disabled = false; }
        }, 1000);
      });
      if (cloud && $("door-account")) {
        for (const r of cloud.REGIONS) $("acct-region").add(new Option(r.label, r.id, r.id === "us", r.id === "us"));
        let savedHint = null, forgetting = false;
        const regionLabel = (id) => cloud.REGIONS.find((r) => r.id === id)?.label ?? id;
        const showSaved = (hint) => {
          savedHint = hint;
          $("acct-saved").hidden = !hint;
          $("acct-form").hidden = !!hint;
          $("acct-explain").hidden = !!hint;
          if (hint) { $("acct-saved-email").textContent = hint.email_hint; $("acct-saved-region").textContent = regionLabel(hint.region); }
        };
        // A PIN refusal on a cloud route: ask once, keep the PIN where post() keeps it, retry once.
        const withPin = async (op) => {
          try { return await op(); } catch (e) {
            if (!(e instanceof cloud.CloudError && e.kind === "box" && e.detail === "pin")) throw e;
            const pin = window.prompt("This box has a PIN. Enter it:");
            if (!pin) throw e;
            sessionStorage.setItem(pinKey, pin);
            return await op();
          }
        };
        const fetchAccount = async ({ saved = false, remember = false } = {}) => {
          if ($("acct-fetch").disabled || forgetting) return;
          const email = $("acct-email").value.trim(), password = $("acct-pw").value;
          $("acct-pw").value = "";
          if (!saved && (!email || !password)) { $("acct-msg").textContent = "Enter the email and password you use in the amaran app."; return; }
          if (saved && !savedHint) return;
          const region = saved ? savedHint.region : $("acct-region").value;
          if (!cloud.REGIONS.some((r) => r.id === region)) { $("acct-msg").textContent = cloud.userMessage(new cloud.CloudError("shape", "region")); $("acct-msg").className = "muted error"; return; }
          const my = ++gen;
          const stale = () => { $("acct-msg").textContent = ""; };  // a database won meanwhile: drop this sign-in's progress line
          // Forget stays off while a sign-in runs (the box also refuses to save one forgotten meanwhile).
          $("acct-fetch").disabled = true; $("acct-refresh").disabled = true; $("acct-forget").disabled = true;
          $("acct-pick").innerHTML = ""; $("acct-pick").onclick = null;
          $("acct-msg").className = "muted";
          try {
            $("acct-msg").textContent = "Signing in on the box…";
            const token = await withPin(() => cloud.loginViaBox(region, email, password, { remember, saved, headers: headers() }));
            // What the door says about the sign-in once the mesh is in: only what the box confirms it holds.
            let keptNote = saved ? ". Your sign-in is saved on this box." : "; nothing stored.";
            if (remember && !saved) {
              const st = await cloud.settledStore({ headers: headers() });
              if (st) showSaved(st.saved);
              keptNote = cloud.confirmsSaved(st) ? ". Your sign-in is saved on this box."
                : st && !st.malformed && st.store === "save failed" ? ". The box could not save your sign-in. It was used for this fetch only."
                : ". The box did not confirm it saved your sign-in.";
            }
            if (my !== gen) { stale(); return; }
            $("acct-msg").textContent = "Fetching your mesh…";
            const pack = await cloud.fetchMesh(region, token);
            if (my !== gen) { stale(); return; }
            const all = cloud.meshChoices(pack);
            const choices = all.filter((c) => c.fixtures > 0 && c.keysOk);
            const noneUsable = all.some((c) => c.fixtures > 0 && !c.keysOk) ? new cloud.CloudError("shape", "mesh keys") : new cloud.CloudError("nomesh");
            if (all.length <= 1 && !choices.length) throw noneUsable;
            const pick = (c) => {
              const res = cloud.toExtracted(pack, c.uuid, caps), n = res.fixtures.length;
              load(res, `Fetched ${n} fixture(s) from your amaran account. Check the ones this box should control, then Save.`, "account");
              $("acct-pick").innerHTML = ""; $("acct-pick").onclick = null;  // drop the closure that holds the pack
              $("acct-msg").textContent = `Signed in${keptNote}`;
              $("acct-email").value = "";
            };
            if (all.length === 1) pick(choices[0]);
            else {
              $("acct-pick").innerHTML = all.map((c) => c.fixtures > 0 && c.keysOk
                ? `<button class="small" data-uuid="${escapeHtml(c.uuid)}">Mesh ${c.index} · ${c.fixtures} fixture(s)</button>`
                : `<button class="small" disabled>Mesh ${c.index} · ${c.fixtures > 0 ? "keys unreadable" : "no fixtures"}</button>`).join("");
              $("acct-pick").onclick = (ev) => {
                const uuid = ev.target.dataset?.uuid; if (!uuid) return;
                if (my !== gen) return;  // stale once another source was chosen
                try { pick(choices.find((c) => c.uuid === uuid)); } catch (e) { $("acct-msg").textContent = cloud.userMessage(e); $("acct-msg").className = "muted error"; }
              };
              if (choices.length) $("acct-msg").textContent = "This account has more than one mesh. Choose the one these fixtures are in.";
              else { $("acct-msg").textContent = cloud.userMessage(noneUsable); $("acct-msg").className = "muted error"; }
            }
          } catch (e) {
            if (my !== gen) { stale(); return; }  // a newer source owns the rows: a stale failure stays silent
            $("acct-msg").textContent = cloud.userMessage(e); $("acct-msg").className = "muted error";
          } finally { $("acct-fetch").disabled = false; $("acct-refresh").disabled = false; $("acct-forget").disabled = forgetting; }
        };
        $("acct-form").addEventListener("submit", (ev) => { ev.preventDefault(); fetchAccount({ saved: false, remember: $("acct-remember").checked }); });
        $("acct-refresh").addEventListener("click", () => fetchAccount({ saved: true }));
        $("acct-forget").addEventListener("click", async () => {
          if (forgetting || $("acct-fetch").disabled) return;
          forgetting = true; $("acct-forget").disabled = true;
          try {
            await withPin(() => cloud.forgetSaved({ headers: headers() }));
            // The box erases on its main loop: say Forgotten only once it reports nothing held.
            const st = await cloud.settledStore({ headers: headers() });
            if (cloud.confirmsForgotten(st)) {
              showSaved(null);
              $("acct-msg").textContent = "Forgotten. The box no longer holds a sign-in."; $("acct-msg").className = "muted";
            } else {
              if (st) showSaved(st.saved);
              $("acct-msg").textContent = st && !st.malformed && st.store === "forget failed" ? "The box could not forget the sign-in. Try again."
                : "The box did not confirm it forgot the sign-in. Try again.";
              $("acct-msg").className = "muted error";
            }
          } catch (e) { $("acct-msg").textContent = cloud.userMessage(e); $("acct-msg").className = "muted error"; }
          finally { forgetting = false; $("acct-forget").disabled = $("acct-fetch").disabled; }
        });
        // At load: no prompt (someone here for the database import must not be asked); a 401 means no saved block.
        cloud.savedLogin({ headers: headers() }).then((r) => showSaved(r?.saved ?? null), () => showSaved(null));
      }
      $("copypath")?.addEventListener("click", () => {
        // The page is plain http, where navigator.clipboard does not exist: select the
        // path and use the legacy copy command, leaving it selected for a manual copy.
        const range = document.createRange(); range.selectNodeContents($("macpath"));
        const sel = getSelection(); sel.removeAllRanges(); sel.addRange(range);
        let copied = false;
        try { copied = document.execCommand("copy"); } catch { /* fall through */ }
        $("copypath").textContent = copied ? "Copied" : "Selected: press ⌘C";
      });
      $("setpin").addEventListener("click", async () => {
        const pin = prompt("New PIN (4–8 digits; 0000 removes the lock):");
        if (pin === null) return;
        const r = await post("/amble/pin", { pin });
        if (!r.ok) { $("msg").textContent = r.error; return; }
        // Queued: the box confirms only once NVS has it. Wait for that, bounded.
        $("msg").textContent = "Storing PIN…";
        const wantSet = pin !== "0000";
        for (let i = 0; i < 10; i++) {
          await new Promise((res) => setTimeout(res, 500));
          let o;
          try { o = pinOutcome(await getStatus(), wantSet); } catch { continue; }
          if (!o.done) continue;
          if (o.ok) { sessionStorage.setItem(pinKey, pin); $("pin").textContent = wantSet ? "PIN set" : "no PIN"; }
          $("msg").textContent = o.message;
          return;
        }
        $("msg").textContent = "The box did not confirm the PIN change; reload to check.";
      });
    };
  }
}
