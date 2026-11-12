# Decisions

Durable rationale for amble-home. Check here before questioning why something is the way it is. Newest first. Superseded entries move verbatim to `docs/decisions-archive.md`.

---

## 2026-11-12 — A lost command is detected from the status reply and re-sent once

**Decision (Claude's implementation; Chris approved the scope and the one known cost on #14).** The box confirms each light command from the first status reply that arrives at least 200 ms after it, and re-sends a command that reply shows was lost, once per command from outside. "Lost" means certain signs only: the wrong power state, another brightness than was sent (a whole-percent cut is the fixture's rounding), the other color mode, or exactly a look the fixture had before the command (its last report, or the look commanded just before). Color temperature, hue and saturation alone are never compared with what was sent.

**Why.** The fixtures do not acknowledge commands, so the status reply is the only confirmation there is, and the box was already asking for it. Comparing color values with what was sent would re-send on every command to a fixture that rounds them, and nothing in the amaran tables says which do. One re-send bounds the airtime and the one cost: a change made in the amaran app within the same half second as a Home Assistant command can be overridden.

**Rejected.** Sending every command twice (doubles airtime in exactly the bursts where loss happens, and still confirms nothing); retrying until the reply matches (a fixture that rounds, or a person using the app, would be fought forever); raising the mesh retransmit count (already three transmissions per command; the measured loss is with that).

## 2026-11-03 — Updates come through a hosted feed the box reads; the flasher page stays the recovery floor

**Decision (Chris, built on the dev board the same afternoon).** Every release build carries ESPHome's `update` component (platform `http_request`) pointing at the flasher page's manifest on GitHub Pages, which CI extends with an `ota` block per chip (the OTA image beside it, its MD5, a summary and the release link). The box fetches the manifest once after boot and every hour, compares the manifest version with its project version, and Home Assistant shows a newer one as an Update row with Install; the box downloads over HTTPS, verifies the MD5 and reboots. The box never installs on its own. The feed lives in `amble-home.update.yaml`, included by the two release configs and not by the bench config: an Install from HA would otherwise replace a dev build with the release image and drop its spare-only allowlist. The manifest is served from GitHub Pages, not GitHub Releases, because Release download URLs redirect to URLs longer than the device's 512-byte buffer (ESPHome's own guidance). A check that fails makes no new offer and leaves the last successful one standing; Install writes the download to the inactive OTA partition and verifies its checksum before switching to it, so a failed download or checksum leaves the current firmware running.

**Why.** It is the one update path a stranger will actually use: no cable, no ESPHome install, one press. It is also what lets the first wave of fixes after going public reach people. ESPHome provides the whole mechanism; the project adds twelve lines of config and a manifest block.

**Rejected.** *Automatic installation:* a box that reflashes itself while someone is lighting a shoot is worse than a stale box. *A separate update manifest:* ESPHome reads the ESP Web Tools manifest with an `ota` extension, so one file serves the flasher and the feed and cannot drift. *Clearing a stale offer on a failed check:* needs an upstream change in ESPHome's component; the stale offer is a real release the box saw, and the checksum check before the partition switch makes it harmless; revisit if ESPHome adds it. *The feed in the shared config:* review found it would let HA Install onto the dev board (D1); hence the release-only package.

## 2026-10-30 — One S3 image without PSRAM and one C6 image, picked by the flasher page from the chip

**Decision (Chris, by choosing the ATOMS3 Lite for production).** The flasher manifest carries two builds: `amble-home.yaml` for every ESP32-S3 board, built for 8 MB of flash with no PSRAM (`amble-home-esp32s3.*`), and `amble-home-c6.yaml` for the ESP32-C6 (`amble-home-esp32c6.*`). ESP Web Tools picks the build from the chip family it reads off the port. Everything the two share with the bench config lives in `amble-home.common.yaml`, included as a package; a top file holds only the chip, the build directory, the LED pin and the release guard.

**Why.** The one-image proof: the 8 MB no-PSRAM S3 image ran the whole stranger path on the HiLetgo N16R8, the Freenove N8R8 and the ATOMS3 Lite and took further updates on each, while the 16 MB PSRAM image hangs a board without PSRAM at boot. Nothing in amble-home needs PSRAM (heap free held at about 140 KB over an hour on the Atom) or more than 8 MB (the S3 image is 1.9 MB against two app slots). The C6 is a different architecture, so one binary cannot serve both chips, and the flasher's chip pick makes the second image invisible to the person flashing. Under ESP-IDF the board id changes nothing but pin defaults, so one S3 build is the whole S3 family.

**Rejected.** *One image per board:* a matrix to build, test and explain, for no difference in the binary beyond the LED pin. *Keeping PSRAM in the S3 image:* hangs the Atom and any board without it, and buys nothing. *Dropping the board LED to be board-neutral:* the devkits' factory-lit LED stays on otherwise; the pin is a no-op where there is no LED. *The C6 as the production board:* a minority platform in ESPHome, single core, and the second image; the Atom is the mainstream chip and the same image strangers get.

## 2026-10-29 — Production updates travel over ESPHome's OTA from the CI image (until a hosted update feed exists)

**Decision (Chris).** A new build reaches the production box over the air: ESPHome's native OTA, pushing the `.ota.bin` that CI built for `main` (`esphome upload amble-home.yaml --device <box> --file amble-home-esp32s3.ota.bin`; the file was `amble-home.ota.bin` before 2026-10-30), after the dev board has run that exact image. Chris: "I'm LOVING doing OTA on the prod box. I'm gonna want to keep doing that. That feels like what I, as a stranger, would want to have happen." This is the "same path a stranger uses" in rule 1 for anyone with ESPHome installed; the flasher page over USB stays the documented first install and the recovery floor.

**Why.** Nothing is unplugged, the box keeps its Wi-Fi, fixtures, PIN and saved sign-in, the upload takes about 25 seconds and the box is back on the mesh within another 15, and the bootloader rolls back to the previous slot by itself if the new image fails to boot (proved in Spike 1). First done 2026-10-29: a CI image to the production box, seven fixtures live on return.

**Rejected.** USB reflash from the flasher page for updates (works, keeps settings when erase is unchecked, but means fetching the box from the shelf and Web Serial in a browser). Waiting for the Home Assistant update button: it needs the hosted manifest, which needs the public repo; it becomes the stranger's path when it exists, and this entry's trigger to revisit.

**How to apply.** Canary first: the CI image goes to the dev board over OTA while its table holds only the P60c (a release image carries no Wi-Fi credentials, and the dev build never saved its compiled-in ones, so the dev board then needs them again over Improv on USB, `docs/bench.md`). Keep the image production ran before on disk until the new one has driven the studio. Never a build CI did not produce.

## 2026-10-29 — The box makes the sign-in call; the browser still fetches the mesh; *Remember* is opt-in and plain

**Decision (Chris).** The cloud door's sign-in moves from the browser to the box. The page sends email, password and region to the box over the home network; the box makes amaran's login call with the two headers amaran requires, hands the token back to the page once, and drops the password. The page fetches the mesh from amaran directly and shows what changed, as built. A check box, off by default, lets the person keep the sign-in on the box in plain storage; then the door offers **Check my account** (renamed *Refresh from amaran* on 2026-10-29) and **Forget**. Checking is always a visit to the Edit page and a press, with the person deciding what to Save. The box never signs in on its own.

**Why.** amaran's login refuses any request without the headers `platform` and `request_source` (`code 10001`), and amaran's cross-origin policy forbids a browser from sending them, so a browser-only login cannot work (reproduced with curl and a real Chrome, 2026-10-29). That is Sidus's correct choice for their users, not a bug to work around. The box is not a browser; nothing stops it. The headers must be present but any value passes, so the box names itself `amble-home` in them rather than claiming to be the app. Chris's ladder, as stated: take-over (no amaran at all, later), database (one-time, obscure, iOS cannot), one-time login (the box holds the password for one call), remembered login with one-click check (the box holds it in plain storage, said plainly), and automatic checks, which he declined. The audience has already trusted the firmware with their Wi-Fi password, which ESPHome keeps in the same plain storage; the README states the ladder and the person chooses. The v1 design of 2026-10-24 had the box signing in; the 2026-10-28 spec moved it to the browser on an untested inference from Spike 4, which this entry corrects.

**Rejected.** *Automatic checks, periodic or at boot:* the box would have to interpret the mesh itself and change HA devices with nobody looking, and a fleet of boxes polling amaran's private API is the footprint most likely to make Sidus close the door for everyone; a boot-time read is the one to revisit if people with the box ask. *A hosted relay that adds the headers:* a third party in a stranger's credential path. *Asking Sidus to allow the two headers for browsers:* they would be right to say no; it also folds into the one note Chris sends. *Hardware flash encryption to protect a stored password:* real, but irreversible per chip, unsupported by ESPHome as a setting, and it breaks flashing from a browser; a different product. *Storing only the token:* it expires. *Encrypting the stored password in software:* theatre, the key would sit on the same chip.

---

## 2026-10-28 — The cloud door is approved: browser-only, on demand, nothing stored, on in public builds

**Decision (Chris).** The Edit page gains an account door beside the database door, behind a choice page with three doors (account, database, take-over grayed as "coming"). The browser signs in to amaran once, fetches the mesh and hands the same payload to the same check-list and Save; the password is used for one call and cleared, the token is discarded after the fetch, nothing reaches browser storage or the box but the resulting table and keys. On demand only, by the user's hand. The door ships enabled in the public build. All three regions are offered. Before going public Chris sends one factual note to Sidus Link and does not wait on a reply. There is no refresh feature: every visit to the Edit page is matched against the box's table by MAC and shows what changed (new, renamed with the old name, gone), so a rename or an added fixture is a re-fetch and a Save, and HA's own rename migration (2026-10-26) carries it the rest of the way.

**Why.** It is the iOS user's only path, and the shape keeps every standing rule intact: the amaran app remains the mesh's source of truth, the box gains no cloud client and no credential, and a change to the undocumented API breaks the door alone, never the page. Chris on refresh: the page "would look the same no matter what"; the first visit is only the case where nothing is pre-checked.

**Rejected** *(the first item is superseded by the 2026-10-29 entry above, which keeps its revisit trigger for automatic checks)*. *A stored token or password for a one-click or HA-side refresh:* the token expires (the login reply carries an expiry; Spike 4 saw a stale one refused), and a password on the box turns a once-per-import call into something that can poll, puts a credential on a device strangers flash from a browser, and is the likeliest thing to turn Sidus Link against the project. Revisit if Sidus Link responds, as its own design. *Shipping the door dark in public builds (the 2026-10-24 default):* leaves the public release with no answer for a phone-only user; the switch stays in `build.mjs` so it can be turned off in an hour. *Hiding China:* nothing in the path is region-specific but a hostname. *Asking Sidus Link's permission:* invites a no; informing them puts the goodwill on record.

---

## 2026-10-27 — Every box is `amble-home-xxxxxx` and its own mesh node at a seeded address; dark only

**Decision.** Release and dev builds set ESPHome's `name_add_mac_suffix`, so a box is `amble-home-` plus the last six hex digits of its MAC on the network, as its setup Wi-Fi and in Home Assistant. The box's own mesh unicast address is never imported: at boot it folds a 16-bit seed into `0x4000..0x7FFF` and steps past any fixture holding that address (`derive_own_address`, host-tested); a payload carrying `own_address` is refused. The seed is random, chosen on the box's first boot and kept in NVS, so the address is stable for the life of the box and an erase almost always gives it an identity it never used: the residual is a 1-in-16384 chance per earlier identity of returning to a remembered address with a restarted sequence number, whose cost is commands ignored until the number catches up and, in theory, repeated encryption nonces on lighting commands; accepted by Chris as policy rather than guarded, with the documented remedy "erase and flash once more" (amended 2026-10-27 after review D4: the first cut derived the address from the MAC, which brought an erased box back at a known address with a restarted sequence number). If the seed cannot be read or stored the mesh stays down. When the node the mesh stack restored from NVS differs from the configuration in address or NetKey, the box records that identity's sequence number and IV index in an identity history (`identity.h`, NVS), discards the node (`bt_mesh_node_reset`) and provisions afresh; an identity used before resumes its sequence number past the record, and a network seen before starts at the IV index it reached. If the record cannot be written the box fails closed. The box also answers `amble-home.local` through a delegated mDNS hostname (`amble: mdns_alias`), a one-box convenience. The pages are dark only.

**Why.** Two boxes on one network and one mesh (Chris's dev and production boxes, and anyone with a spare) must not share a name, and must not share a mesh address: a node's address carries its sequence number, and fixtures drop any message whose sequence went backwards for that source, so two boxes on one address silently lose whichever is behind. The amaran app numbers fixtures from the bottom of the address space; the top half is clear of it for any realistic table, and 16384 random slots make a collision between two boxes a 1-in-16384 chance, unguarded and accepted. A fresh sequence space is safe for an identity this box has never used, because replay protection is per (network, source); for one it has used before, only resuming past the recorded sequence number is safe, which is what the identity history guarantees (and why it never evicts, treats an unreadable record or an unanswerable store as a stop, applies its sequence floor on the restore path, and re-provisions at the recorded IV index when a restored node sits below it). Dark only: one scheme to verify, the lamp swatches read best on black, and the amaran app itself is dark.

**Rejected.** *A user-settable box name (WLED style):* ESPHome fixes the name at build time and it is the hostname, mDNS name, HA device name and entity-id prefix; overriding it at runtime fights ESPHome (rule 4) and ripples into entity ids. HA renames cover the display name; revisit if public users ask. *Four-character suffix:* ESPHome's rule is six; no reason to be different. *Keeping a fixed own address (`0x00F1`) and asking the user to pick:* nobody who uses the flasher knows what a mesh address is. *Deriving the address from the MAC (the first cut):* predictable, so an erase returned the box to an address the fixtures remembered; the random seed keeps every property that mattered (unique per box, invisible, clear of fixtures) and drops the one that hurt. *Fail closed on an identity mismatch (the old behavior):* it would have stranded every existing box on this very change, and a user importing a different mesh's database the same way. *A light color scheme:* the accent that reads as tungsten on black needed darkening to pass contrast on white and lost its character.

## 2026-10-26 — Three guard modes: dev, release, production

**Decision.** `amble: release: true` lets the guard address exactly the unicast addresses in the box's own fixture table (refilled from NVS at every boot); group/broadcast stay compiled out. `allow_group: true` (Chris's studio only) adds group on top and requires `release: true`. The default stays the dev mode: a fail-closed allowlist of the spare, which `release: true` forbids. Validation refuses every other combination.

**Why.** A stranger's box must reach its own lights and nothing else; the fixture table is the only thing on the box that says which lights are its own, and an imported-but-unchecked fixture is deliberately not in it. Keeping dev as the default means a config that forgets `release:` can hurt nothing.

**Rejected.** *Release = no guard (any unicast):* an address typo or a stale table could reach a neighbor's fixture on a shared mesh. *An allowlist strangers edit in YAML:* nobody who uses the flasher edits YAML.

## 2026-10-26 — The flasher is ESP Web Tools on GitHub Pages with ESPHome's factory image at offset 0

**Decision.** `web/flasher/index.html` loads ESP Web Tools 10 from unpkg and a `manifest.json` next to it with one build (`ESP32-S3`, `amble-home.factory.bin` at offset 0). *(Two builds since 2026-10-30, one per chip family; see that entry.)* CI builds the release config from the repo alone, renders the manifest with the version (tag, else `0.0.0+sha`), checks it (`web/flasher/check.mjs`) and publishes the images; a `v*` tag makes a GitHub Release. Hosting on Pages waits for the public repo.

**Why.** ESPHome's factory image is a merged flash image, so one part at offset 0 is the whole install; ESP Web Tools adds Improv Wi-Fi and the "add to Home Assistant" link for free. The spec constrains flashing to a web page over HTTPS.

**Rejected.** *ESPHome's hosted dashboard flow:* needs the ESPHome add-on; strangers may not have it. *Separate bootloader/partition/app parts:* more to get wrong, no benefit over the factory image.

## 2026-10-26 — Entity identity is the MAC-anchored key; renames ride on Home Assistant's own migration

**Decision.** Each fixture's entities are registered with a key hashed from `amble_<last three MAC bytes>`, never from the name. ESPHome 2026 sends only that key, the name and a device id; Home Assistant builds the unique id from the name but, on reconnect, migrates a registry entry whose key and device are unchanged and whose name changed. Bench-verified 2026-10-26 with Chris's HA: the P60c renamed to "amaran P60c bench" and back kept the same light, number and button registry entries and entity ids, with their unique ids rewritten in place and no second entity. The spec's naming rule therefore holds for free: a user's HA name override survives, automations keep their entity ids.

**Why.** It needs no new mechanism and no ESPHome change. The entity id slug keeps the name the fixture had when first seen (standard HA behavior); the display name follows the amaran name.

**Rejected.** *One ESPHome sub-device per fixture with a constant entity name ("Light"):* the cleanest identity (unique id `mac/device_id/light/Light`), but `EntityBase::set_device_` is codegen-only (friend access) and `ESPHOME_DEVICE_COUNT` is fixed at codegen, so runtime sub-devices would need a patch or a fork. Revisit if HA's rename migration ever regresses, or for the upstream ask (a public ESPHome API to register a component created at runtime).

## 2026-10-26 — The box's pages and sql.js are embedded in the firmware at codegen

**Decision.** `web/box/build.mjs` assembles the Home and Edit pages, the importer library, the capability table and sql.js into `web/box/dist/`; `components/amble/__init__.py` gzips each file into a generated, gitignored header (`assets_gen.h`) that `web.cpp` serves with `Content-Encoding: gzip`. About 0.4 MB of flash against 7.9 MB per OTA slot. The build is deterministic (gzip mtime 0) so an unchanged `dist/` does not rebuild the firmware.

**Why.** One artifact to flash, one OTA to update, no second partition to manage, and the Edit page works with no internet (sql.js is not fetched from a CDN, so a key-bearing database is parsed by code the box itself served). ESPHome's own `web_server` embeds its page the same way.

**Rejected.** *A SPIFFS/LittleFS data partition:* a second thing to flash and keep in step with the app, and a second OTA story. *Loading sql.js from a CDN:* the import page would then depend on the internet and on a third party's script while a key-bearing file is open in the browser.

## 2026-10-26 — The Edit PIN gates the mutating routes, not the pages

**Decision.** `POST /amble/import`, `/amble/identify` and `/amble/pin` require the `X-Amble-Pin` header when a PIN is set (NVS key `pin`, 4–8 digits, `0000` or absent means no gate). Five wrong attempts lock the gate for 30 s; a correct PIN during the lockout is refused too. The static pages and `GET /amble/status` stay open. Plain HTTP, as the spec accepts: a casual-access lock for a shared LAN, not transit security.

**Why.** The pages hold nothing a stranger on the LAN could use; what matters is that nobody can change the box's configuration or flash a fixture without the PIN. Gating the data routes makes that true regardless of how the page is reached.

**Rejected.** *HTTP Basic auth on everything:* browsers cache it awkwardly and the status page should stay glanceable. *HTTPS:* certificate pain on an ESP for a LAN lock, per the spec.

## 2026-10-26 — Status poll every 15 s per fixture, staggered; HA does not rely on overhearing

**Decision.** Each configured fixture is asked for its status when nothing else has asked within `status_poll` (default 15 s; `0s` disables), staggered 150 ms apart, never while that fixture already has a request outstanding. A command's own status request resets the fixture's timer.

**Why.** Spike 2's live-app check showed the box does not overhear the app's traffic when the app proxies through the lone fixture. Eight fixtures at one request per 15 s is about 0.5 messages per second on a shared mesh, which the production bridge's own polling already exceeds.

**Rejected.** *No poll:* HA drifts from reality whenever the app or a wall switch is used. *60 s:* visibly stale for a light someone just changed in the app.

## 2026-10-26 — The release tripwire is the mesh send-failure counter, not a patched adv-pool ghost count

**Decision.** With no IDF patch (decision 2026-10-25), the old bridge's `adv_buf_ghost` counter cannot exist. `Mesh send failures` (the stack refused to queue a message, `-ENOBUFS` included) is the diagnostic that must stay at zero; status timeouts, refused commands and unknown sources sit beside it.

**Why.** The ghost count existed to catch the adv-pool leak before it surfaced; its symptom *was* `-ENOBUFS` on send. Counting the symptom needs no patch and catches any other cause too.

## 2026-10-26 — Rule 2 scope: strict where it ships, relaxed in the private workshop

**Decision (Chris):** anything derived from a user's `amaran.db` stays out of firmware logs, tool output and anything that reaches strangers; the product identifies fixtures as "fixture N" in logs. Chris's own fixture names and mesh addresses may appear in this private repo's issues and docs. Keys and MACs appear nowhere, ever.

**Why:** the firmware ships to people who will paste their logs into public bug reports; a log line naming a fixture and its address leaks their mesh layout and we would be the ones who printed it. The private repo is Chris's workshop and the public repo is curated, so scrubbing his own names here buys nothing.

**Rejected:** fully strict (redact items 2 and 6, slot labels everywhere): cost with no benefit inside a private tracker. Keys-and-MACs-only everywhere: names and addresses in shipped logs are still a leak path for other users.

## The box's HTTP surface is `web_server_base` plus our own routes; ESPHome's `web_server` is never included (October 25, 2026)

**Context.** The spec's anti-goal: the box controls nothing; ESPHome's built-in web-server entity controls must be disabled, not merely unused. ESPHome's `web_server` component ships those controls for every entity.

**Decision.** The reference config includes `web_server_base` only. `components/amble/web.cpp` starts it and registers two routes: `GET /amble/status` (read-only JSON) and `POST /amble/import` (configuration, never control). The Edit/Home pages of milestone 3 are added the same way. The anti-goal then holds by construction.

**Alternatives rejected.** *`web_server` with controls hidden per entity:* configuration-dependent and easy to regress. *A separate HTTP server:* duplicates what `web_server_base` already provides.

**How to apply.** Never add `web_server:` to a config. Any new route is read-only or configuration-only; handlers run on the IDF HTTP task (4.5 KB stack): copy the request, hand real work to the main loop, heap-allocate anything bigger than a few hundred bytes.

## Runtime entities drive their own lifecycle; the component does not register them as ESPHome components (October 25, 2026)

**Context.** Fixtures are runtime data, so the lights exist only after NVS is read at boot. ESPHome 2026 sizes its entity tables from codegen counts (reservable via `CORE.register_platform_component`) but its component table from a count fixed before external components run; a runtime `register_component_` silently drops the entry. Bench symptom: the light published state but never called `write_state`.

**Decision.** `AmbleComponent` creates each `LightState`, registers it as an entity (so the API sees it) and calls its `setup()`/`loop()` itself via `Component::call()` from its own loop. Numbers and buttons are plain entities and need no component slot.

**Alternatives rejected.** *Codegen 32 placeholder lights named at boot:* 32 entities in HA for every box regardless of fixtures. *Reaching into `Application`'s protected registration:* the table is full anyway. *Asking ESPHome upstream for a runtime registration API:* worth doing (noted), not something to wait for.

**How to apply.** Any further runtime component (none planned) is driven the same way. Keep the reservation loop in `components/amble/__init__.py` in step with the entity types created at boot.

## Going public means a fresh, curated public repository; this one stays the private workshop (October 25, 2026)

**Context.** The design says the repo stays private until Chris is satisfied, possibly past milestone 4, and GitHub Pages (the hosted flasher) needs a public repo on the free plan. Public visibility is permanent and covers the issue tracker as well as the commits: spikes, candid review threads, bench fumbling, and the notes on amaran's undocumented cloud API would all be readable.

**Decision.** At publication this repository is renamed and remains the private workshop, history intact. A new repository takes the `amble-home` name with a clean, curated history and only the issues worth re-filing. Chris: "I love the messy all being saved, but I don't want anyone discovering all it took to get there." Standing from now: nothing in an issue or commit we would be unhappy to see public; curation at publication does not excuse carelessness.

**Alternatives rejected.** *Publish as-is:* honest and free, exposes the cloud-API notes and the whole story. *Rewrite history in place:* one-way, and issues stay public anyway.

**How to apply.** Keep working candidly here. Revisit at milestone 4 (the decision issue, created closed, holds the mechanics to settle: naming/transfer, which docs carry over, seeding the public issues).

## ESP-IDF is whatever ESPHome pins (5.5.x today); the mesh layer aims to carry no IDF patch, with the re-ported snoop patch as the fallback (October 25, 2026)

**Context.** The studio bridge needed two ESP-IDF patches: 0001 (decrypt a copy of fixture status replies, which are addressed to the app's `0x0001`, inside the network receive path) and 0002 (a lock on the mesh advertising buffer pool). ESPHome 2026.10 pins IDF 5.5.6 and builds it natively (CMake + ninja); there is no pre-build hook, and the only framework override is a URL to a complete IDF tarball. Carrying a patched IDF would mean hosting and re-rolling a multi-hundred-megabyte tarball on every ESPHome release, and every user building from source would need it too. Spike 1 found that 5.5.x already contains the 0002 fix upstream, and that the receive entry point 0001 hooks (`bt_mesh_generic_net_recv`) is reached from other object files by every bearer — so the linker's `--wrap` catches it — while the IDF exports every primitive (de-obfuscation, network decrypt, app decrypt, subnet/app-key accessors) a side-effect-free snoop in our own code needs.

**Decision.** amble-home builds against ESPHome's pinned IDF, unmodified, following ESPHome's pin as it moves. Patch 0002 is dropped. For the status snoop the plan of record is a **linker-wrap** on `bt_mesh_generic_net_recv` that lets the real function run untouched and then decodes a copy of the raw PDU in `amble_mesh` (de-obfuscate, net-decrypt per subnet key, app-decrypt, hand the Telink status to the light), with its own small duplicate filter. Spike 2 must prove it against a live fixture; if it fails, the fallback is the re-ported patch (`spikes/01-fit/patches/0001-amaran-net-recv-status-snoop-idf55.patch`, applies cleanly to 5.5.6) delivered via `esp32.framework.source` pointing at a patched tarball we host.

**Alternatives rejected.** *Host a patched IDF tarball* (`esp32.framework.source`): works, but puts a ~GB artifact and a re-roll on every ESPHome bump between a user and a build; kept only as the fallback. *PlatformIO `extra_scripts` to patch in place:* ESPHome's native IDF build ignores `platformio_options`. *Make the box's element address `0x0001` so replies are "for us":* reintroduces the shared-identity hazard the design review flagged; never. *Keep 0002:* already upstream.

**How to apply.** Don't fork or vendor ESP-IDF. Mesh code that needs an internal symbol includes the IDF's internal headers (the native build already exposes `esp_ble_mesh/core`, `core/include`, `common/include` to project sources) and links the wrap with `cg.add_build_flag("-Wl,--wrap=…")`. Also set `CONFIG_BT_GATTS_ENABLE` in the mesh component: the mesh GATT bearers need it and `esp32_ble` only enables it when a BLE server/client component is present. When ESPHome bumps the IDF, re-check three things: the wrapped symbol still exists and is still called cross-object, `bt_mesh_pre_provision`/`bt_mesh_provision`/`bt_mesh_node_local_app_key_add`/`bt_mesh_node_bind_app_key_to_model` still exist, and the exported crypto/key accessors are unchanged.

## Configuration is a WLED-shaped, box-served page; the box reports and configures but never controls lights (October 24, 2026)

**Context.** Setup must serve desktop, phone-only and no-account users without a second UI to maintain and without the box becoming a control surface that competes with Home Assistant. ESPHome already runs a web server on the device, and HA already controls entities.

**Decision.**
- The device is **WLED-shaped**: flash it, it joins Wi-Fi, you browse to the box itself. The box serves two pages over the LAN (reusing ESPHome `web_server`): a read-only **Home** (every configured fixture, shown live / dark / absent) and an **Edit** page (import, pick which fixtures to control, identify-flash). The box also serves its own web assets, including the database parser, so the page and its upload are same-origin and dodge CORS and mixed-content. Only the flasher is web-hosted (on GitHub Pages); there is no separate authoring app and no custom HA integration or Lovelace card.
- **Zero light control on the box, ever.** No slider, no color picker, no on/off. The box reports and configures; Home Assistant is the only place lights are controlled. This is an anti-goal, not a preference.
- **Setup-page access** is PIN-gated (PIN set at first-run, default `0000` means no gate); a casual-access LAN lock, keys and token never shown back. Plain HTTP accepted on a home LAN; HTTPS not worth the cert pain on an ESP.
- **Naming:** amaran's name is the entity's provided name; HA wins the moment the user renames there. New, removed or renamed fixtures reach HA on reboot, because ESPHome re-announces entities only on reconnect; a table-changing refresh reboots, an unchanged one does not.

**Alternatives rejected.** *A table or control UI inside Home Assistant* (custom integration or Lovelace card): a second codebase in a foreign ecosystem, more to maintain, against the ESPHome-owns-everything rule. *A separate GitHub Pages importer app*: two UIs; the box-served page covers both first import and ongoing edits. *Any light control on the box*: rebuilds WLED and HA both.

**How to apply.** Config/import code lives in the `amble` component's box pages; the cloud part is a build-flagged `amble_cloud`. The import payload is producer-agnostic. See the spec's Setup section and the key-source decision below.

**Amended 2026-10-24 (Codex cross-model review).** Three refinements, all in the spec: (1) **concurrent live use** of the amaran app and the box shares one mesh identity (`0x0001`) and is a real hazard (sequence collisions, nonce reuse); it is wanted but held, and proven/guardrailed adversarially in Spike 2 before shipping, not assumed. (2) **OTA recovery floor is USB reflash**; automatic rollback is a nice-to-have, not a promise. (3) Dev builds enforce a **fail-closed transmit allowlist** (only the configured spare-fixture address), because unicast-only does not keep the dev board off the production fixtures it imports.

## amble-home exists; it is a port of the studio bridge onto ESPHome, not a fork of anyone's repo (October 24, 2026)

**Context.** Chris's Home Assistant control of seven amaran fixtures ran on a hand-maintained ESP-IDF bridge (`clwill/amaran-BLE-control`, branch `bridge-local`) descended from wesbos's research repo. The plan as of 2026-10-23 was to contribute its fixes to Arne Brockmann's Stream Deck fork. Reflection showed the two projects share only the mesh protocol: a Stream Deck plugin lives on a host PC with specific control software; a Home Assistant device is a different product with a different surface. Chris also did not want to front conversations about C internals he has no interest in reading.

**Decision.**
- A new, separate, public repo, `clwill/amble-home`, MIT, independent of the author's home-automation repository.
- Built as an **ESPHome external component** plus a web flasher/importer. ESPHome supplies Wi-Fi, captive portal, Improv, OTA, native HA API, logging, web server. amble-home supplies the mesh bridge and nothing ESPHome already has.
- **Ownership is stated in the README:** Chris owns and directs; Claude writes and maintains the software. Chris does not read the code.
- The amaran app remains the mesh's source of truth. No provisioning, no fixture-management UI; setup is "import from the app's data". Rename-in-app is not reflected live because names never travel on the mesh; re-import is the answer.
- Contributions to Arne's fork continue as a courtesy (the minimal mesh-health tripwire PR he asked for), then stop. Arne is told, and is welcome to borrow from amble-home.

**Alternatives rejected.**
- *Keep contributing to Arne's fork.* Different product; our HA code (discovery, diag sensors) has no home there; Chris fronting it is dishonest in a way he dislikes.
- *Own a fork of wesbos's repo.* Already retired on 2026-10-23 when Arne agreed to take PRs; a fork also inherits the MQTT/HTTP/Wi-Fi code ESPHome makes redundant.
- *Rewrite the mesh layer from scratch.* No reason; the protocol code is small, proven for months and MIT.

**How to apply.** New code goes in `components/amble/`; anything that smells like network lifecycle, OTA or discovery is an ESPHome component configuration, not C++. The bridge stays production until a planned cutover (`AGENTS.md` rule 1).

## Reference hardware is the ESP32-S3 N16R8, and it is the only supported target; the classic ESP32 is not supported (October 24, 2026)

*(Amended 2026-10-30: the supported set is the README boards table, ESP32-S3 and ESP32-C6, with one image each; see the 2026-10-30 entry above. The classic ESP32 stays unsupported.)*

**Context.** The bridge runs on a classic ESP32 with a single 3 MB factory partition and no OTA because the app sits near 2 MB. ESPHome OTA needs two app slots, which on 4 MB flash leaves roughly 1.9 MB each before NVS and the ESPHome overhead. The mesh stack (~1 MB) plus ESPHome plus the box web assets puts amble-home in the same ~2 MB-plus class as the bridge, which cannot fit twice in 4 MB. (Amended same day, before any build: the founding stance kept the classic ESP32 as a degraded target; Chris reversed it.)

**Decision.** The ESP32-S3 DevKitC-1 N16R8 (16 MB flash, 8 MB PSRAM, native USB) is the reference, minimum and **only supported/tested** board. OTA is always available on it, USB flashing is the recovery path. The **classic ESP32 4 MB is explicitly not a supported target**: it cannot hold two OTA slots, it is a second build and hardware check for no real gain, and a 4 MB owner's fix is to buy a roughly $13 S3. Chris's own studio board is a classic ESP32-WROOM-32 (confirmed from the bridge's `sdkconfig`); he replaces it with an S3 at cutover.

**Alternatives rejected.** *Keep the classic ESP32 as a degraded, no-OTA tier* (the founding stance): a second target to build and smoke-test, orphaned OTA, for a board a user can replace for the price of lunch; contrary to the latest-and-supported ethos. *ESP32-C3/C6*: single core, smaller; still being refined in ESPHome; not the field's mesh board. *ESP32-P4*: no built-in radio.

**How to apply.** Build and test only the S3. "Latest" means latest ESPHome + ESP-IDF on the S3, not newer silicon. Resist "but the P4 is newer" and "but my old 4 MB board."

## Keys and fixtures come from one of three doors, cloud preferred; the phone-only story is resolved by the cloud door (October 24, 2026)

**Context.** Mesh keys and the fixture table have two known sources: the amaran Desktop app's SQLite database (macOS, Windows) and the amaran cloud. The cloud was confirmed this session to return the net key, app key, IV index and fixture table to a signed-in client (the Desktop app's `login_by_password` then `getProjectDetail`). amaran's main surface is iPad/iPhone, whose local database is unreadable without a jailbreak. (Supersedes the founding "setup requires the Desktop database; phone-only is open" stance, same day.)

**Decision.** Three doors, all producing the identical on-box result:
- **a. Cloud login (preferred).** The box signs in to amaran and downloads keys + fixtures; it keeps a token, never the password. Works from any device including a phone, and is **the phone-only answer**. Enables one-click rename-sync. **Behind a compile-time switch, compiled out of public builds** until Chris enables it, so the reverse-engineered client is never published by default.
- **b. Database file (fallback).** Parsed in the browser on the machine that holds `amaran.db`; only keys + fixtures reach the box, never the raw file.
- **c. Take-over (nuclear, later milestone).** The box re-provisions fixtures into its own mesh; the amaran app loses them. Last resort for someone with neither a usable cloud login nor a database. Standing rule 3 (`AGENTS.md`) is amended to allow this one deferred exception; the UI slot stays non-operational until a provisioning design is proven on the bench, and capability-via-pairing is not assumed (not established by `docs/origin.md`).

**Alternatives rejected.** *Desktop database as the only v1 path* (the founding stance): excludes phone-only users needlessly, now that the cloud door exists. *Shipping the cloud client in public builds by default*: publishes a reverse-engineered third-party API client; the compile-time switch defuses this without losing the feature. *Sniffing keys from the air*: the NetKey/AppKey never travel in the clear; dead end.

**How to apply.** Keys and the fixture table arrive at the box as one producer-agnostic payload, so a new source slots in without redesign. The cloud part is a build-flagged `amble_cloud`. See the spec's Setup section and the configuration-architecture decision above.
