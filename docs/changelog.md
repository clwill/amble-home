# amble-home changelog

Build history, newest first. Entries older than about 30 days move to `docs/changelog-archive.md`.

---

## Unreleased

- **The device page names the release.** `esphome: project:` carries `clwill.amble-home` and the tag's version (CI passes it as the `amble_version` substitution; a local build says 0.0.0), so Home Assistant shows "amble-home 1.0.1" beside the ESPHome version instead of a compile date.
- **Flasher page copy.** "Check" the erase box, and the hosted update feed is planned rather than promised.
- **Known bug, #10.** One light can stay unavailable in Home Assistant after the box reconnects; the wiki's Troubleshooting page carries the reload workaround.

## 1.0.0 (2026-11-03) — first public release

- **What it is.** An ESPHome firmware that puts amaran (Aputure) Bluetooth-mesh lights into Home Assistant: flash from the browser, join Wi-Fi from the same page, import your fixtures from your amaran account or the desktop app's database, and the lights appear as Home Assistant lights with on/off, brightness, color temperature, color and green/magenta, their state read back every 15 seconds.
- **Boards.** One image for every ESP32-S3 board with 8 MB of flash or more and one for the ESP32-C6; the flasher page picks by chip. Five boards have run the whole path (the wiki's Boards page).
- **The hosted flasher.** A tag builds the images, creates the GitHub Release and publishes the flasher page, the manifest and the images to GitHub Pages, so the page always offers the latest release.
- **The record.** The wiki carries the user-facing pages; `docs/` carries the design, the decisions and the fixture matrix; the private development history is summarized below.
 — how v1 was built (2026-10-24 to 2026-11-02)

amble-home was designed, built and put into production in ten days, in a private repository that remains the author's development record. What follows is the shape of that work; the public history starts at 1.0.0.

- **Design (2026-10-24).** The v1 design (`docs/superpowers/specs/2026-10-24-amble-home-design.md`) was approved on the founding day and revised the same day after a cross-model review closed six defects. The ownership model, the standing rules and the Codex + Claude working agreement in `AGENTS.md` date from then.
- **Spikes (2026-10-25 to 2026-10-28).** Four measured questions before any product code: the amaran mesh stack builds, fits and runs under ESPHome on an ESP32-S3 with OTA rollback proven (`spikes/01-fit`); key injection works on ESPHome's pinned IDF, a fixture obeys unicast commands, and status can be overheard without patching the IDF (`spikes/02-mesh`); a static page can open the amaran desktop app's database in the browser with sql.js and pull keys and fixtures out of it (`spikes/03-importer`); the amaran cloud's sign-in and mesh calls, captured once, shapes only (`spikes/04-cloud`).
- **Milestone 2 (2026-10-25 to 2026-10-26).** The mesh node, the Telink codec, the fixture table, the NVS store and the transmit guard; the first fixture became a Home Assistant light with on/off, brightness, color temperature, color and green/magenta, driven and read back.
- **Milestone 3 (2026-10-26).** The box's own Home and Edit pages, the database door, the PIN, the status poll and quiet diagnostics; entity identity anchored to the fixture's hardware address so renames in the amaran app ride Home Assistant's own migration.
- **Milestone 4 (2026-10-26).** The release build with its guard modes, the browser flasher on ESP Web Tools, CI that builds the images and the flasher manifest from the repository alone, and a README. Bare board to Home Assistant in under five minutes on the bench.
- **Every box its own node (2026-10-27).** Each box is `amble-home-xxxxxx` after its own hardware address and a mesh node at an address seeded at first boot; dark only, never a provisioner.
- **The cloud door (2026-10-28 to 2026-10-29).** Sign in to your amaran account from the Edit page instead of finding a database: the box makes amaran's sign-in call (a browser cannot), the browser fetches the mesh; the password is used once and not stored unless *Remember* is checked; the Edit page became a choice page with the take-over door shown as coming.
- **Production (2026-10-27 to 2026-10-31).** The author's studio moved from the earlier MQTT bridge to amble-home in phases with a rollback at each step: seven lights, six scenes, a group and a dashboard, then eight lights; production takes new images only as a deliberate over-the-air update after the bench board has run them.
- **The board matrix (2026-10-28 to 2026-11-01).** One S3 image for every ESP32-S3 board (8 MB of flash, no PSRAM) and one C6 image, picked by the flasher page from the chip it finds; five boards ran the whole stranger path and drove a fixture: an ESP32-S3 N16R8 devkit, the Freenove ESP32-S3, the M5Stack ATOMS3 Lite, the Waveshare ESP32-C6 and the Seeed XIAO ESP32S3. Production moved to the ATOMS3 Lite after 24 hours side by side with no difference.
- **The last stranger-facing edges (2026-10-30 to 2026-11-01).** Duplicate fixture records and two fixtures on one mesh address no longer refuse an import; long CJK names keep whole characters; the flasher's dialog offers **Visit Device** once the box is on Wi-Fi; the Windows path verified end to end in Edge; the Edit page puts every door's controls above its explanation.
