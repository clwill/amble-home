# amble-home design

**Status: APPROVED 2026-10-24** by Chris, in the founding design session; revised the same day to close the six defects from the Codex cross-model review. Per this spec the throwaway spikes come before the implementation plan; see Spikes and Milestones.
**Author:** Claude, from the 2026-10-24 brainstorm with Chris. Items marked *(Claude's call)* are implementation choices Chris delegated; items marked *(open)* need Chris.
**Reads first:** `AGENTS.md`, `docs/origin.md`, `docs/decisions.md`.

## Intent

Put amaran BLE-mesh studio lights into Home Assistant as first-class devices with the full ESPHome experience: flash from a browser, join Wi-Fi via the device's own access point, OTA updates, native HA discovery. Setup is "tell the box about your fixtures"; the amaran app stays the authority on the mesh. A stranger with an ESP32 and an amaran account should get their lights into HA in under fifteen minutes without a toolchain.

The device is **WLED-shaped**: you flash it, it joins your Wi-Fi, and you browse to the box itself to set it up. The box is home base, not a thing configured from elsewhere. Everyday control happens in Home Assistant, not on the box.

Why now: the studio bridge works but is a hand-built ESP-IDF app whose Wi-Fi, MQTT, HTTP and OTA-less lifecycle ESPHome already does better; its maintenance was heading into a Stream Deck-oriented fork where Home Assistant code has no home; and Chris wants a project whose software is openly Claude's to write and maintain.

## Goals

1. Each fixture is an HA `light` with on/off, brightness, colour temperature within the fixture's range, and HS colour and green/magenta where the fixture supports them. State reflects fixture status replies, not just the last command.
2. Flash from a browser (ESP Web Tools on GitHub Pages). Provision Wi-Fi via the captive portal or Improv. OTA thereafter.
3. Get keys and fixtures onto the box by one of three doors, preferring the amaran cloud; persist them on the device. Re-import (or a cloud refresh) is the only fixture-management operation.
4. The cloud door makes app-side renames flow through: rename in the amaran app, hit refresh, the new name appears. (Reopens what the mesh alone made impossible: names never travel on the mesh, but the cloud re-serves them.)
5. Diagnostics in HA: mesh adv-pool free, ghost count (tripwire, must stay 0), send-completion counters, Wi-Fi RSSI, uptime.
6. Replace the studio bridge in Chris's office in a planned cutover with rollback, preserving the six HA scenes.
7. A maintainer agent: scheduled Claude routine plus GitHub Actions that triage issues, review PRs and cut releases, with a canary device as the hardware gate and Chris holding the merge button.

## Non-goals

- Provisioning fixtures or managing mesh membership in the ordinary case. The amaran app does that. (The nuclear take-over door is the sole, explicit exception; see Setup.)
- Live reflection of app-side renames *over the mesh*. Still impossible; the cloud refresh is how renames arrive instead.
- MQTT. The native ESPHome API is the HA transport. (Migration note below.)
- Effects, scenes or Sidus-style presets in firmware. HA scenes do this.
- DMX, Art-Net, or the Sidus cloud as a control path.

## Anti-goals (would make this a failure even if it technically works)

- **The box app controlling lights. Zero. None.** The box page reports state and configures fixtures. It has no slider, no colour picker, no on/off. **ESPHome's own built-in web-server entity controls and write endpoints are disabled, not merely unused — a read-only custom Home page alone does not establish this.** Control is Home Assistant's job and only HA's. The instant the box grows a control, we have rebuilt both WLED and HA and lost the plot.
- A dev or test build ever moving a production fixture Chris did not intend to move. Group/broadcast addresses are forbidden outside an explicit "I am production" configuration, **and dev builds additionally enforce a fail-closed allowlist: only the configured spare-fixture address may receive any command (see Dev/production guard).**
- A user's mesh keys or amaran password leaving their browser or device for any server of ours: no upload, no keys in logs, YAML or issues.
- A device bricked with no way back. Recovery is guaranteed by **USB reflash** (every board can always be re-flashed over USB); automatic OTA rollback is attempted where ESPHome/IDF supports it but is a nice-to-have, not a promise. *(Chris's call 2026-10-24: "oops, reflash over USB" is an acceptable floor; Spike 1 documents the actual behaviour.)*
- A failed cloud refresh wiping a working fixture table. The saved table is sacred; a refresh only ever adds to or updates it, never erases it on error.
- Chris having to read a source file to make a decision.

## Constraints

- ESPHome framework `esp-idf`. The mesh layer needs two ESP-IDF patches (`docs/origin.md` 4 and 5) unless the pinned IDF already carries the adv-lock fix; the status snoop patch is permanent as long as we snoop. ESPHome allows a custom framework `source:` and `sdkconfig_options`, so a patched IDF is possible.
- Key injection uses internal IDF calls (`bt_mesh_node_local_app_key_add`, `bt_mesh_node_bind_app_key_to_model`). They must be re-verified on the pinned IDF.
- **Mesh coexistence is a narrower question than first written.** The box joins the mesh as its own node with its own unicast address, well clear of the fixture block (the studio bridge has always used `0x00F0`), sharing only the NetKey and AppKey with the amaran app. It does not and must never claim the app's `0x0001`. *(Corrected 2026-10-25: the earlier text said the bridge sent as `0x0001`; reading its source shows it never did, so the "two senders on one address" hazard from the design review does not exist.)* Coexistence with a live app is therefore the ordinary case of several nodes on one mesh, with two things still to prove adversarially in Spike 2: that the box's sequence number and IV index survive reboot and power loss so the fixtures' replay protection never rejects it (the IDF zeroes the sequence on self-provision), and that the old bridge can resume at `0x00F0` after amble-home has transmitted — or amble-home takes a different address. Until Spike 2 proves it, the "use the amaran app alongside, live" promise is held, not shipped. The box tracks IV index and sequence and persists the sequence across reboots so a post-reboot number does not fall behind. *(Claude's call: persist the sequence in coarse blocks, not every message.)*
- Reference, minimum and **only supported board: ESP32-S3 N16R8** (16 MB flash, 8 MB PSRAM, native USB), the newest-and-mature choice for BLE mesh. OTA is always available on it, with USB flashing as the recovery path. "Latest" means latest ESPHome + ESP-IDF on the S3, not bleeding-edge silicon (C6 is single-core; C6/H2/P4 still being refined in ESPHome; P4 has no built-in radio). **The classic ESP32 4 MB is explicitly not a supported target.** It cannot hold two OTA slots, it is a second build and hardware check for no real gain, and the fix for a 4 MB owner is to buy a roughly $13 S3. Chris's own studio board is a classic ESP32-WROOM-32 (confirmed from the bridge's `sdkconfig`); he replaces it with an S3 at cutover. This supersedes the founding hardware decision in `docs/decisions.md`, reconciled there at approval.
- Browser flashing (ESP Web Tools / WebSerial) works only in Chrome or Edge on a computer, never Safari and never a phone. Flashing is always a desktop act; configuration afterward is any browser or phone.
- The amaran cloud API is undocumented and reverse-engineered; it can change without notice, and a login may demand a verification code a headless box cannot complete. The database door is the fallback for exactly those cases. **The cloud door is behind a compile-time switch: public release builds compile it out entirely — the code, the box's cloud UI, everything — so the reverse-engineered client is never published in a public binary until Chris explicitly enables it. Private builds include it.** In a public build the database door is the front door. This is how the legality concern is defused without losing the feature. Its limits, stated plainly: compiling the cloud door out keeps it out of the *public firmware binary*, but it is not legal clearance and it does not hide the source in a public repo. If a box already configured for cloud is later flashed with a public (cloud-less) build, it keeps its working local fixtures, stops all cloud activity, and discards its stored token. Public onboarding documents the database door as the available path until any other door ships.
- Chris's production fixtures share the mesh with the dev board. Unicast-only in dev builds.

## Architecture *(Claude's call)*

```
┌──────────── browser on a computer (GitHub Pages) ──────────┐
│ flasher: ESP Web Tools → USB serial → firmware             │
└─────────────────────────────────────────────────────────────┘
                     │ one-time, over USB
┌──────────── ESP32 (ESPHome), reachable at amble.local ─────┐
│ wifi · captive_portal · improv · ota · native HA API       │
│ web_server + amble pages (served BY the box, on the LAN):  │
│   HOME  read-only status of every configured fixture       │
│   EDIT  authoring: cloud / database / take-over → table    │
│ components/amble:                                          │
│   amble_mesh   owns BT controller + Bluedroid + mesh;      │
│                key injection; vendor tx; status snoop      │
│   amble_cloud  signs in to amaran, fetches keys+fixtures,  │
│                holds the refresh token, refreshes on boot  │
│   amble_light  one LightOutput per configured fixture      │
│   amble_diag   sensors (pool free, ghosts, completions…)   │
│   NVS: keys, fixture table, IV index, sequence, token      │
└─────────────────────────────────────────────────────────────┘
          │ native API (control + diagnostics + buttons)
       Home Assistant   ← the only place lights are controlled
```

- **One external component, several parts.** `amble_mesh` is the only code that knows the mesh. It initialises the Bluetooth controller and Bluedroid itself; whether ESPHome's `esp32_ble` can host the controller instead is a spike question.
- **The box serves its own UI and its own assets.** The authoring/config surface, including the sql.js/WASM used to parse a database file, is served by the device from flash over the LAN (reusing ESPHome's `web_server`), not hosted on GitHub Pages. So the Edit page and its POST back to the box are same-origin plain HTTP, with no CORS and no mixed-content problem. Only the flasher lives on Pages, because flashing must be web-hosted. This is the "one table, one codebase" answer: no second UI in HA, no custom HA integration or Lovelace card. *(Claude's call. The ~1 MB of web assets is nothing on the 16 MB S3.)*
- **Fixtures are runtime data, not YAML.** The YAML declares the component once. Fixture `light` entities are built at boot from the NVS table, keyed by fixture MAC (stable across re-imports, so HA names and scenes survive). An empty table yields a device with diagnostics only and a "waiting for setup" status. *(Claude's call: validated to a studio-scale bound of about 32 fixtures; not a hard protocol cap, but the point past which memory, especially on the classic board, is re-measured.)*
- **The saved (active) table is the source of truth the box runs on.** It lives in NVS and works offline. The cloud, when reachable, only freshens it. The underlying **atomic commit** — activating one coherent configuration (mesh identity, keys, selection, source mode) and rebooting only if something changed — is a shared operation with two triggers. A **manual import**, especially of a different source, produces a **candidate** that does not become active until you press **Save**; Identify and a cancelled setup never switch the active configuration either. A **validated automatic refresh of the already-active source** invokes the same commit directly — updating names and fixtures within that source, preserving your selection and the sequence/IV safety state. A failed or cancelled import, or a failed refresh, leaves the active configuration unchanged, and re-importing an older database never rolls back the persisted sequence/IV state.

## Setup and import (the box's two screens)

### Three doors, preferring the cloud

All three produce the identical result on the box: the net key, app key, IV index and a fixture table (MAC, mesh address, name, capabilities). From the box's side, it cannot tell which door was used.

- **a. Cloud (preferred, build-gated).** You enter your amaran email and password on the box's Edit page. The password goes to the **box**, which signs in to the amaran cloud and downloads your keys and fixtures. (The box makes the call, not the browser, because a browser cannot call amaran's cloud directly — cross-origin.) The box trades the password for a token, keeps the token, and discards the password. Works from any device including a phone. Refreshable (below). This is the only door that supports the rename-sync feature, and the only one that works for phone-only users. **This entire door is behind a compile-time switch and is absent from public release builds until Chris enables it (see Constraints); when it is off, the Edit page shows only database and take-over.**
- **b. Database (fallback).** You open the box's Edit page **from the computer that holds `amaran.db`** (the amaran Desktop app's SQLite file; macOS path in `docs/origin.md`, Windows box available for testing). You pick the file; the page parses it **in your browser** (sql.js served from the box) and sends only the extracted keys and fixtures to the box. The raw file never leaves your machine. Point-in-time snapshot; refreshed by re-importing. Because parsing is in-browser, any browser that can see the file works, including iOS Safari if the desktop `amaran.db` is reachable via the Files app (iCloud/share) — the phone's own amaran database stays sealed and is not a source.
- **c. Take-over (nuclear, later milestone).** No account, no database. The box provisions fixtures into **its own** mesh with **its own** keys. You lose amaran app control and amaran firmware updates; getting back means factory-resetting and re-adding each light in the app, and that return is **manual** — not an automatic restoration of prior addresses, groups or HA references. It is a distinct mode, not mixable with a or b (different keys). A fixture already in amaran's mesh is not pairable until you factory-reset it, so take-over's table fills in one light at a time as you reset them. Whether provisioning yields each light's model code, and thus its capabilities, is **not established by `origin.md`** and must be proven; until then do not assume capability-via-pairing. **This door is the one explicit, deferred exception to `AGENTS.md` standing rule 3**, which otherwise forbids provisioning and mesh-membership management; rule 3 is amended to carve it out. The Edit page carries the slot from the start but it stays **non-operational** until a separate provisioning design — provisioning/config state, device-key persistence, capability discovery, interrupted setup, return to app ownership — is written and proven on the spare fixture.

### The Edit page (authoring)

A table, read-only names, one row per fixture:

```
[ ] <name>   <mesh id>
[ ] <name>   <mesh id>
[save] [id] [refresh]
```

- **Checkbox** selects fixtures. One selection, two uses: which fixtures to control in HA (on Save) and which to flash (on ID).
- **Names are read-only here.** From amaran in doors a/b; `AH_<meshid>` in take-over. Renaming happens in HA (see naming rule). This keeps the box out of the naming business and lets the cloud rename-sync work.
- **Save** commits the current candidate: it atomically activates one coherent configuration — mesh identity, keys, fixture selection and source mode together — then reboots so the new entity set reaches HA (see entity model). A crash mid-commit leaves the old active configuration intact; there is no half-written state. (An automatic refresh of the already-active source uses this same atomic commit without a manual Save; a manual import of a new or different source does not.)
- **ID** flashes the checked fixtures (ramp to ~50% white and back) so you can map a row to a physical light. It works before Save, but on **candidate** keys held only for the session and bounded so it cannot change the active mesh; an import or a cancelled setup never switches the running configuration. Imported keys stay candidate until Save makes them active.
- **Refresh** is mode-dependent: (a) re-fetch from the amaran cloud; (b) re-prompt for a database file; (c) scan the mesh for fixtures you have reset into pairing.

### The Home page (status, read-only)

The default page. Every **configured** fixture, with a liveness column so three states read at a glance:

- **Not configured:** absent from Home; exists only in Edit as something you could import.
- **Configured but dark:** present, greyed with `<NA>` where state would be. Saved but not answering (powered off, out of range, or a mesh hiccup — the box shows that it is dark, not always why).
- **Live:** present in full, name and real state (on/off, colour, brightness).

No controls. Ever. An optional, minimal box "dashboard" of fixture info may exist but is not load-bearing; real diagnostics go to HA as entities.

### Access to the setup page

The Home/status page is open and read-only. The **Edit page is gated by a PIN**. The flasher hands off, right after writing the firmware, into the first-run setup (the same flow that collects Wi-Fi), and that is where the PIN is chosen. It is stored in NVS and **defaults to `0000`, which means no gate** — so the default is frictionless, and setting any other PIN turns the lock on. This is a casual-access lock for a shared home LAN: it keeps other people and devices off the setup page. It is **not** transit encryption — traffic stays plain HTTP, only HTTPS would hide the password on the wire, and that is not worth the certificate pain on an ESP. The password is on the wire only at the moment of entry, then discarded for a token, and stored keys and the token are never shown back through the UI. *(Claude's call: the PIN gates Edit routes only; PIN length and any rate-limiting are builder details.)*

### Entity model, refresh and boot

- **New, removed and renamed fixtures reach HA only on reconnect.** ESPHome registers its entities when the native API connects; it does not add, remove or rename entities on a live connection. So any change to the fixture table — a fixture added or dropped at Save, or a name changed by a cloud refresh — takes effect when the box rebuilds its entities at boot and HA reconnects. Pure state (on/off/colour/brightness) is live and needs no reboot. **Therefore a table-changing operation reboots the box; an unchanged refresh does not.** This matches Chris's own "reboot to refresh" instinct and keeps the mechanism honest.
- **Boot-time cloud refresh, time-bounded.** In cloud mode the box uses its stored token to re-fetch on every boot, but the attempt is time-boxed so unreachable internet can never postpone local control. On timeout or failure (no internet, cloud down, token expired) it leaves the saved table untouched and runs it. Database and take-over boots simply run the saved table.
- **Refresh reconciliation.** A refresh distinguishes the fixtures *available* from the source from the fixtures *you selected*: it updates names and addresses of your selected fixtures, surfaces newly-available and newly-missing ones without silently dropping your selection, and tells a network failure apart from a genuinely empty result (an empty valid result never deletes your table).
- **Refresh from HA.** The box exposes a native **restart** control and a **re-sync** button entity. Re-sync fetches from the cloud for the already-active source and, only if something changed, commits atomically (preserving selection and sequence/IV state) and reboots to propagate it. So "I renamed a light, pull it" is one button press; it costs a brief mesh drop because a name change must re-register the entity, which is unavoidable given the entity model.
- **Identify from HA.** A per-fixture identify **button** entity flashes that light, for relabeling after import. (A command, not an entity-structure change, so no reboot.)

### Naming rule

Home Assistant wins, for free. The amaran name arrives as the entity's provided name. The moment you rename an entity in HA, your name sticks and later refreshes leave it alone; clear your HA name and it falls back to the (now current) amaran name. Automations, dashboards and scenes bind to the MAC-anchored entity id, never the display name, so no refresh can break them. *(Verify the "HA keeps the provided name updated underneath an override" behaviour on the dev board; it is standard HA registry behaviour but unproven here.)*

## Light semantics *(Claude's call)*

Brightness 0–1000 on the wire ↔ HA 0–255. Colour temperature in Kelvin within `cct_min..cct_max`. HSI when colour is set; CCT otherwise (mutually exclusive on the fixture; the light exposes `color_mode`s accordingly). Green/magenta as a per-fixture `number` entity.

**Status, liveness and airtime.** State comes primarily from passively snooped status replies (the IDF patch 0001 path). A command with no matching status within a timeout retries once, then logs and leaves state as commanded — it does **not** flip the HA entity to unavailable on a single miss. "Dark" on the Home page is a box-side display judgement: no status seen within a liveness window. A low-rate liveness status request may back this up, kept deliberately sparse because the mesh is shared with production and airtime is not free. Liveness drives the Home page colour, not HA availability. Snooped status is authenticated and checked for freshness and duplicates (the mesh replay/sequence rules apply to replies too) and filtered to our fixtures, so a stale or replayed reply cannot masquerade as current state. *(Claude's call; the cadence and the snoop checks are verified in Spike 2.)*

## Dev/production guard

Two independent guards. First, group and broadcast sends (`0xC000`, `0xFFFF`) are compiled out unless the YAML substitution `amble_allow_group: true` is set, which only the production config sets. Second, and separately, a dev build enforces a **fail-closed destination allowlist at the single transmit boundary: only the locally-configured spare-fixture unicast address may receive any command.** Import, selection, refresh and Identify cannot widen it, and every non-unicast or non-allowlisted destination is rejected. Unicast-only is not enough on its own, because the dev board shares the production mesh and the imported table carries the seven production fixtures' unicast addresses; the allowlist is what actually keeps a dev session off them. The spare's address is kept out of tracked files and logs.

## Diagnostics

ESPHome `sensor`/`binary_sensor` entities with `entity_category: diagnostic`. The ghost tripwire is a sensor whose nonzero value is the bug. These are the signals the canary device reads to gate releases.

## Spikes (throwaway, before the plan)

1. **Fit, build, and OTA recovery.** ESPHome esp-idf build with the mesh stack on the S3: compiles against ESPHome's pinned IDF with patches 0001/0002 via a forked `source:`; app size; two OTA slots fit comfortably on the S3's 16 MB. Also determine what a bad OTA actually does: flash a deliberately-broken image and see whether the previous app returns on its own (ESPHome/IDF rollback and image-confirmation behaviour) or whether recovery means a USB reflash — either is acceptable (USB recovery is the guaranteed floor), the point is to document which, and to confirm an older image can still read config a newer one wrote. Output: numbers in the changelog, the IDF-version decision, the documented recovery behaviour.
2. **Mesh hello, and does coexistence actually hold.** Spike-1 build + a hand-made NVS blob toggles the spare P60c by unicast. Proves key injection on the pinned IDF and settles, **adversarially**, whether the box coexists with the app and the old bridge as its own node: alternate app-and-box commands, reboot, power loss, and return to the old bridge, watching for dropped commands and sequence/IV trouble — a light that moved once is not proof. *(Re-worded 2026-10-25.)* Also exercise Bluetooth controller ownership versus the chosen Improv path; sequence/IV persistence across reboots and its NVS-write cadence; status-reply authentication, freshness and duplicate handling through the snoop patch; the inherited `CRPL 20` replay-cache capacity against a ~32-fixture target (`docs/origin.md` 13–15); and the liveness-poll cadence on the shared mesh. Output: a log line, a light that moved, and a yes/no on safe concurrency (with the guardrail if one is needed).
3. **Database importer.** A static page opens `amaran.db` with sql.js and prints the fixture table and key *presence* (never values). Output: the SQL and capability mapping ported from `generate-config.ts`.
4. **Cloud capture (new).** Watch the amaran Desktop app sign in and sync **once** (on macOS or the Windows box) to confirm the login and project-detail request/response shapes and how requests are signed. Read-only; touches nothing on the mesh; no credentials recorded. Output: the exact request format the `amble_cloud` part must reproduce. Endpoints already read from the app bundle: regional hosts `us/eu/cn/gs.sidus.link`, `login_by_password`, `/mesh/getProjectList`, `/mesh/getProjectDetail` (returns `net_key`, `app_key`, fixtures), refresh-token flow. **Cleared to run; it is read-only and records no credentials. The client it informs is behind a compile-time switch, compiled out of public builds until Chris enables it.**

## Milestones (sketch; the plan refines)

0. Design approved; dev board and spare fixture on the bench.
1. Spikes 1–4 done; decisions recorded.
2. `amble_mesh` + one `amble_light` on the dev board; spare P60c fully controllable from HA via the native API.
3. Box pages (Home + Edit), cloud and database doors, NVS persistence, full fixture table from a real account/`amaran.db`, diagnostics, identify + re-sync.
4. Flasher page; OTA proven on the S3; `amble-home.yaml` reference (S3); README walk-through. Repo stays **private until Chris is satisfied, which may be past this milestone**; going public is what lights up the hosted flasher.
5. Studio cutover: flash an S3 for production, production YAML (`amble_allow_group: true`), scene migration with a pre-cutover scene snapshot; rollback = reflash the old bridge on the old board **and** restore the scene snapshot, with old-bridge control reconfirmed. Old bridge and its classic ESP32 retired only after that.
6. Take-over door.
7. Maintainer agent and release pipeline; public announcement; Arne pointed at it.

## Migration note (Chris's office)

The six HA scenes reference the bridge's MQTT light entities. Moving to the native API changes entity ids, so reflashing the old bridge does **not** by itself restore office lighting — the scenes would still point at the retired entities. Plan: before cutover, snapshot the six scenes and their entity references (via the existing `studio_scene_snapshot.py` tool) and keep the old board and its persistent state restorable. Bring amble-home up under new ids alongside the bridge (unicast only, so both coexist — subject to Spike 2's coexistence finding, since the old bridge must be able to resume at its own `0x00F0` after amble-home has transmitted), rewrite the scenes to the new ids, and verify. Rollback is therefore two moves, both acceptance conditions for the cutover: reflash the old bridge **and** restore the scene snapshot, with the old bridge confirmed controlling the lights again. Not before milestone 5.

## Open questions *(open, for Chris)*

1. **Name collision check.** `amble-home` is free on GitHub as of 2026-10-24. PyPI/npm not checked; not needed unless the importer ships as a package.

*(The studio board is a classic ESP32, WROOM-32 / ESP-32S, 4 MB, no OTA, confirmed from the bridge's `sdkconfig`. Not a supported target for amble-home; Chris replaces it with an S3 at cutover.)*

*(Resolved this session: **phone-only users** — the cloud door is the answer, with take-over as the nuclear fallback. **Cloud-door legality** — build it behind a compile-time switch, compiled out of public builds until Chris enables it, so nothing risky is published by default; the read-only capture spike is cleared to run. **Public vs private** — private until Chris is satisfied, possibly past milestone 4. **Classic ESP32** — explicitly not a supported target; the S3 is the reference, minimum and only target, with OTA always available. A 4 MB owner buys a ~$13 S3, and Chris moves his own studio board to an S3 at cutover. Supersedes the founding hardware decision in `docs/decisions.md`, reconciled there at approval. **Setup-page security** — the Edit page is PIN-gated, PIN set at first-run setup, default `0000` means no gate; a casual-access LAN lock, not transit encryption.)*

*(Resolved from the Codex review, same session: **Standing rule 3 is amended** to carve out the deferred take-over exception, so the rule and the approved scope agree; dropping take-over would strand phone-only users with no cloud, which Chris judged harsher than take-over. **Concurrent live use of the amaran app and the box is held, not promised** — Chris wants it (he has seen it work casually) but it is proven and guardrailed adversarially in Spike 2 before it ships. **OTA recovery floor is USB reflash** — "oops, reflash over USB" is acceptable to Chris; automatic rollback is a nice-to-have Spike 1 characterises, not a guarantee.)*

## Left to the builder

IDF version pin and patch delivery; NVS schema (keys, table, IV index, sequence, token) and sequence-persistence cadence; exact HA colour-mode mapping; the box page's build tooling (static, no framework, is the default); the same-origin POST payload shape (producer-agnostic); token storage and refresh-failure specifics; mDNS name and the Android/Windows IP fallback; GitHub Actions layout for firmware builds and the Pages site; the maintainer-agent canary (milestone 7) — where it lives, how CI reaches it (self-hosted runner or HA read-back), and its unattended ceiling — deferred to that milestone, with the only now-impact being that one of the three boards is reserved as the canary.
