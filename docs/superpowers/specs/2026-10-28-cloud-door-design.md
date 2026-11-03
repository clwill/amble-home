# The cloud door: import from the user's amaran account

**Status:** approved by Chris 2026-10-28; built and reviewed on branch `cloud-door` (plan `docs/superpowers/plans/2026-10-28-cloud-door.md`). **Amended 2026-10-29 (Chris): the box makes the sign-in call, not the browser**, after the real sign-in showed amaran refuses a browser-only login (see *Amendment*). Plan tasks 8–12 build the amendment. Spike: `spikes/04-cloud/README.md`.

## Intent

A person whose fixtures live only in the amaran phone app has no `amaran.db` to hand the Edit page: iOS gives no file access, and most people never touch a desktop app. Their mesh is in their amaran account, because the app syncs it there. The cloud door is the Edit page's second source: sign in, fetch the mesh, tick fixtures, Save. Same payload, same firmware path, same result as the database import. This is the "iOS use model" on the go-public list.

Chris, 2026-10-28: "We can discuss later the issues with cloud access (amaran being upset that we're doing this, and do we do constant updates or on demand only?) but building it seems right anyway."

## Amendment 2026-10-29: the box signs in; the browser does the rest (Chris's decision)

**What broke.** The real sign-in failed in every region. amaran's login answers `code 10001 ParamIsInvalid` unless the request carries two headers the app sends, `platform` and `request_source` (both required, any value accepted, the other six app headers optional; bisected with curl 2026-10-29). amaran's cross-origin policy lets a browser send only `Content-Type, AccessToken, X-CSRF-Token, Authorization, Token, X-Token, X-User-Id`, so a page cannot add them: Chrome blocks the request at preflight. A browser-only login is therefore impossible as amaran stands. The query call needs neither header (a bad token gets the token error with or without them), so once a token exists the browser fetches the mesh as built. Spike 4's "accepted calls without them" was never tested; it was inferred from calls that had them.

**Decision (Chris, 2026-10-29).** The password goes to the box. The page posts email, password and region to the box over the home network; the box makes the one login call with the two headers, hands the token back to the page once, and drops the password. The page fetches the mesh from amaran directly, as built, and shows what changed. Nothing else moves to the box: no mesh parsing, no picker, no automatic apply.

**Remember, opt-in.** The account door gains a tick box, off by default: "Remember this sign-in on the box". Ticked, the box stores email, password and region in its own flash (NVS, the same plain storage ESPHome keeps the Wi-Fi password in) after a successful login. From then on the door opens with the saved sign-in named by a hint (`c…@example.com · United States`) and two buttons, **Check my account** (renamed *Refresh from amaran* on 2026-10-29, which also dropped the other-account link and put every door's controls above its explanation) and **Forget**; a link offers a different account. Checking is still a visit to the Edit page and a press: the page shows what changed and the person decides what to Save. The box never signs in on its own: no schedule, no boot-time read (both considered and declined 2026-10-29: they need the box to interpret the mesh and change HA with nobody looking, and a fleet of boxes polling amaran is the footprint most likely to close the door for everyone; boot-time read is the one to revisit if people ask).

**The promise, in the README's words.** "Your amaran password goes to the box over your home network, is used for one sign-in, and is not stored, unless you tick *Remember*. Then the box keeps it in plain storage next to your Wi-Fi password until you press *Forget*."

**Identification.** The box sends `platform: amble-home`, `request_source: amble-home` and a User-Agent naming amble-home and the repository. amaran accepts any value in the two headers; the box says who it is rather than claiming to be the app. (If a real login is refused with honest values, the fallback is the app's values `7` and `4`, recorded as a ruling.)

**Routes on the box** (all PIN-gated like `/amble/import`, all absent from a build with `AMBLE_CLOUD_DOOR=0`): `POST /amble/cloud/login` with `{email, password, region, remember}` or `{saved: true}` starts the sign-in (one at a time); `GET /amble/cloud/login` reports `idle` (with the saved hint or `null`), `busy`, `done` with the token exactly once, or `failed` with the stage (`transport`, `http`, `json`, `code`, `token`), HTTP status and amaran code; `POST /amble/cloud/forget` erases the saved sign-in. The box never logs the email, the password, the token or amaran's body: log lines carry the stage and the codes only.

**Superseded by this amendment:** Goal 2's "the box never sees the password" (now: the box never stores it unless asked); the non-goal "a box-side HTTPS client"; the anti-goals "the password reaching the box" and "sending the app's identity headers"; *Calls* item 1 (`login` moves to the box; `cloud.js` gains `loginViaBox`); *Identification*; the Spike 4 bullet on the app's headers. The rest of this document stands.

## What Spike 4 established (the facts this design rests on)

- Two plain JSON calls over HTTPS, no request signing: `POST https://<region>.sidus.link/uc/user/login` with `email`, `password` (as typed), `area` (two-letter region) returns `data.token`; `GET https://<region>.sidus.link/amaran/pack/query?mesh_update_time=0&fixture_update_time=0&...` (every `*_update_time` cursor at `0`, `attr_tags=fixture`) with the `token` header returns `data.meshs.existing[]` (`mesh_uuid`, `net_key`, `app_key`, `fixtures_ordered_list`) and `data.fixtures.existing[]` (`mesh_uuid`, `node_address`, `mac_address`, `code`, `name`, `node_name`, `device_key`, ...). Success is `code == 20000`; failures carry a `code` and a `msg` (some in Chinese).
- The servers answer cross-origin requests from the box's page: the preflight from `http://amble-home.local` gets 204 with the origin echoed, methods `POST, GET`, and allowed headers including `Content-Type` and `Token`. The app's descriptive headers (`platform`, `device_name`, ...) are not in the allowed list. *(Corrected 2026-10-29: two of them, `platform` and `request_source`, are required by the login, so a browser cannot log in; see the Amendment. The query needs neither.)*
- Regions: `us`, `eu`, `cn`; `gs` is global services (not needed).
- The record carries no IV index; the database import already defaults it to 0.
- The phone app had synced Chris's mesh without him doing anything: the fixture cursor matched his last rename in the app.

## Goals

1. The Edit page opens with one question, "Where are your fixtures?", and three doors: **sign in to your amaran account** (email, password, region, one button), **upload the app's database** (today's door), and **take-over** (shown greyed, "coming", not clickable; standing rule 3). The fetched fixtures land in the same tick-list, with the same ID and Save behaviour, producing the same v1 payload (`net_key`, `app_key`, fixtures with `mac`, `address`, `name`, `code`, caps).
2. Nothing is stored unless asked. *(Amended 2026-10-29.)* The password is cleared from the field before the call, travels to the box for one login, and is dropped there; the token is handed to the page once and discarded after the fetch. No browser storage. The box stores the sign-in only when the person ticks *Remember*, and forgets it on *Forget*.
3. On demand only. One sign-in per import, the user's choice, never a schedule, never a background poll. The amaran app stays the source of truth (standing rule 3): to change fixtures, change them in the app, come back, sign in again. There is no separate "refresh": every visit to the Edit page is the same page, matched against what the box already holds (see *Names and what changed*).
4. Honest and loud. The page identifies itself by its own origin and sends nothing that pretends to be the amaran app. Every failure says what happened in the user's terms: wrong password, no mesh in this account, amaran's servers refused the browser, no internet from this browser.
5. Works for the iOS user end to end: phone app synced → box on the home Wi-Fi → Edit page on the phone's browser → sign in → fixtures appear in HA.

## Non-goals

- Continuous sync, or any "refresh" that works without a sign-in: that needs a stored password or token and is this document's anti-goal. Revisit trigger (Chris, 2026-10-28): a response of any kind from Sidus Link; then a separate design, with the credential question at its centre.
- Writing anything to the amaran cloud (import confirmation, device registration, feedback).
- Scenes, groups, presets, cameras: only the mesh keys and the fixture table.
- ~~A box-side HTTPS client.~~ *(Amended 2026-10-29: the box makes the login call; the browser still makes the mesh call.)* The box never parses the mesh.
- Provisioning, mesh membership, the take-over door (standing rule 3).

## Anti-goals (failure even if it works)

- The password or token reaching a log, browser storage, or a URL; the password stored on the box without *Remember* ticked. *(Amended 2026-10-29.)*
- The page or the box polling amaran's servers; the box claiming to be the amaran app (it sends its own name in the two required headers). *(Amended 2026-10-29.)*
- A silent partial import (a fixture missing because a field was absent) with no message.
- A broken cloud (amaran changes the API) that makes the Edit page itself unusable: the database door must keep working, and the cloud door must fail by itself.

## Constraints

- The API is undocumented and belongs to Sidus Link; it can change without notice. The door therefore validates every field it uses and fails closed with a message naming the database import as the fallback.
- The browser must have internet while on the Edit page. On the box's setup network (`amble-home-xxxxxx` AP) there is none: the door says so and points at the database import.
- The public build switch. `docs/decisions.md` (2026-10-24) holds that the cloud client is behind a compile-time switch, compiled out of public builds until Chris enables it. The door is page code, so the switch is a build flag in `web/box/build.mjs` (`AMBLE_CLOUD_DOOR=0|1`), default on for Chris's builds; Chris decides the default for the public release (open question 1).
- Key handling is identical to the database door: keys exist in page memory until Save, then only on the box (NVS). Standing rule 2 holds.

## How it works (the parts decided here)

**UI, Edit page.** A choice page: "Where are your fixtures?" with three doors side by side (stacked on a phone): the account door, the database door, take-over greyed with "coming". Opening the account door shows email, password (type `password`, autocomplete off), region (select: United States, Europe, China; default United States) and a "Fetch fixtures" button. While fetching: button disabled, "Signing in…" then "Fetching your mesh…". On success the door collapses to one line ("Fetched 8 fixtures from your amaran account; nothing stored"; the cloud mesh record carries no name) *(Amended 2026-10-29: "Signing in on the box…"; the done line ends "nothing stored." only when Remember was not ticked and no saved sign-in was used, else "Your sign-in is saved on this box."; the door also carries the Remember tick box and, when a sign-in is saved, the saved block with Check my account and Forget.)* and the tick-list, ID and Save flow take over unchanged below it. On failure the message appears in the door; the database door is untouched. The database door keeps its file picker and the "Where is amaran.db?" hint.

**Names and what changed.** The page has no first-time mode and no refresh mode. Every visit, whatever the door, the fetched list is matched against the box's status by MAC (`rowsFor` today): fixtures already on the box come up ticked with their live dot, the rest unticked. On a box that already has a table, the page adds one line ("2 new since the box was last saved, 1 renamed, 1 no longer in your account") and marks each row as one of four states: unchanged; new in the account; renamed, showing the previous name; gone from the account. A gone row is listed unticked and cannot be ticked, and the line says plainly that Save removes it from this box: Save sends a whole table built from the fetched list, and a fixture the account no longer holds has no row to send (the box's status carries no MAC to rebuild one from). The database door already drops such a fixture on Save; it just never said so. This is standing rule 3 in action: the app removed it, the box follows. A rename reaches HA by Save and reboot exactly as decided in `docs/decisions.md` (2026-10-26): the entity key is MAC-anchored, HA migrates the registry entry in place, entity ids never change, and a name the user set inside HA wins over the amaran name.

**Calls, in `web/importer/cloud.js`** (new, shared with tests; no DOM):
1. ~~`login(region, email, password)`~~ *(Amended 2026-10-29)* `loginViaBox(region, email, password, {remember})` → `POST /amble/cloud/login` on the box, then polls `GET /amble/cloud/login` until `done` (token) or `failed`. The box makes `POST https://{region}.sidus.link/uc/user/login`, JSON `{email, password, area}`. amaran `code != 20000` → the credentials message with the code; `code 10001` → the shape message (our own request was malformed, never the person's password); a box refusal (PIN, busy) → its own message.
2. `fetchMesh(region, token)` → `GET https://{region}.sidus.link/amaran/pack/query?` with the eleven `*_update_time` cursors at `0` and `attr_tags=fixture,camera,microphone`, the exact query the spike proved (`spikes/04-cloud/cloud-replay.py`); a trimmed `attr_tags` was never tried against the server and is not worth a first failure in a stranger's hands. Header `Token: <token>` (the allowed header name). Returns the reply's `data` (`meshs.existing[]`, `fixtures.existing[]`), validated.
3. `toExtracted(mesh)` → the same object shape the database extractor returns (`{keys: {net_key, app_key, iv_index: 0}, fixtures: [{mac, address, name, code, caps}], groups}`), so `toImportPayload` and the Edit page's row logic need no change. Fixture caps come from `code` through the same product tables the database path uses; a fixture whose `code` is unknown gets the database path's treatment (listed, capabilities unknown, importable).
4. The token is held in a local variable only; `password` is passed straight to `login` and the field is cleared before the call returns.

**Multiple meshes.** If the account holds more than one mesh, the page shows a picker ("Mesh 1 · 8 fixtures", in the account's order; the record has no name) before the tick-list. One mesh per import, as the database door takes one.

**Fixtures filtered.** `node_address >= 2`, valid MAC, belonging to the chosen `mesh_uuid`; others are listed as skipped with the reason, as the database door does.

**Failure messages (exact copy to be set in the plan):** wrong credentials; account has no mesh; region mismatch (try another region; amaran accounts are regional); network or CORS failure ("Your browser could not reach amaran's servers from this page. Import from the app's database instead.") *(Amended 2026-10-29: "The box could not reach amaran's servers. On its own setup Wi-Fi there is no internet: join your home network, or import from the app's database instead."; a box refusal has its own copy: PIN, locked, busy; amaran's 10001 reads "amaran refused the box's sign-in request (code 10001). The database import still works.")*; unexpected shape ("amaran's reply did not look like a mesh; the database import still works").

**Build flag.** `build.mjs` includes the panel and `cloud.js` only when the flag is on; the page without it is byte-identical to today's.

**Identification.** *(Amended 2026-10-29.)* The box sends `platform: amble-home`, `request_source: amble-home` and an amble-home User-Agent on the login; the browser sets `Origin` on the mesh call and nothing else.

## Verification

- `web/importer/cloud.test.mjs`: unit tests against the recorded shapes (`spikes/04-cloud/shapes.jsonl`): success mapping, `code != 20000`, missing fields, several meshes, fixtures filtered.
- Manual, on the dev board with Chris at the keyboard once: sign in on the Edit page, fetch, tick the P60c only, Save; the box reboots with the table; the P60c responds (hardware evidence on the issue). Production is never touched; the studio fixtures are never ticked.
- Review lane: escalated (the import page reads a key-bearing source; user credentials pass through the page). Codex CLI bridge by default.

## Settled by the builder check (Claude's calls, 2026-10-28)

A fresh reviewer read this document as the builder and asked eight questions; the answers, with evidence:

1. **Capabilities without a hardware version.** The cloud record has no `control_hw_version` and its `composition_data` is identical across hardware versions (checked in the bench database: hw 1.0, 1.1 and 1.2 units of one code share it). It does not matter: in `fixture-caps.json` every code's capability fields (colour, GM, CCT range) are identical across its hardware-version entries; only the version label differs. The lookup by `code` alone is exact. The extractor passes no version; `capsFromConfig` takes the lowest entry, which equals the others.
2. **Record states.** Only `existing[]` is used; `deleted[]` is ignored (nothing to remove: the box holds what the user ticks). `binding` and `fixtures_ordered_list` are not used as filters; `fixtures_ordered_list` is an opaque string whose format was not captured, so the fixtures sort by address as the database door does.
3. **Formats.** The cloud stores what the app writes to its database: keys as 32 hex characters (upper case in the database; the importer lower-cases), MACs as `AA:BB:CC:DD:EE:FF`. The extractor normalises case and accepts a MAC with or without colons, then applies the same strict checks (`HEX32`, `MAC_RE`) and fails loudly on anything else. The header is `Token` (the name in the server's allowed list; header names are case-insensitive).
4. **Wiring and "byte-identical".** Refactoring `box.js` to accept a second source is allowed and expected (a "load result" seam used by both doors). The requirement is behavioural: with the flag off, the cloud panel and `cloud.js` are absent from `dist/` and the database door's tests and flow are unchanged. Byte-identity of `box.js` is not required.
5. **Flag mechanics.** Markers in `edit.html` like the theme markers; `cloud.js` copied into `dist/` only when the flag is on; the Node tests run the cloud tests regardless (pure JavaScript). The checked-in `edit.html` carries the panel inside its markers; `build.mjs` strips it when the flag is off.
6. **Origins and CSP.** amaran's preflight echoes any origin: tested `http://amble-home.local`, the box's full `.local` name, its bare IP and a port-suffixed form, all 204 with the origin reflected. The box sends no content-security policy. The CORS failure copy stays generic because a refusal would mean the server changed.
7. **Picker edge cases.** Meshes with no fixtures are listed but cannot be chosen (greyed, "no fixtures"); fixtures whose `mesh_uuid` matches no listed mesh are ignored and counted in the skipped list.
8. **Region mismatch.** The server's codes for a wrong region are unknown (not captured). One generic message covers wrong credentials and wrong region: "amaran did not accept that email and password for this region. If your account was created in another region, choose it and try again." No automatic retry across regions: one sign-in per attempt, by the user's hand.

## Decided by Chris (2026-10-28)

1. **Public build default: on.** `AMBLE_CLOUD_DOOR=1` for every build, the public release included. The door is the iOS path.
2. **amaran's goodwill: one note.** Chris sends one short, factual message to developer@sidus.link before the public repo goes live and does not wait on a reply. The case: the user's own credentials on the user's own data, the same two calls the app makes, once per import, nothing stored, nothing written back.
3. **On demand only: yes.** No separate refresh feature; the Edit page is the refresh (see *Names and what changed*). A sign-in-free refresh is a later design, after Sidus Link's stance is known.
4. **Region list: all three.** A Chinese account exists only on the China server; hiding it would break import for those users with no explanation.

## Left to the builder

File layout within `web/box/edit.html`/`box.js` and `web/importer/`; exact copy; the picker's look (the visual system in `theme.css`); how the build flag is passed to `build.mjs`.
