# Bench notes

How a dev board and a Mac get along. Procedure, not history; rewrite lines in place as they change. Build history is in `docs/changelog.md`.

## The board

Any board from the README's table works as a dev board; the ESP32-S3 N16R8 devkit (two USB-C ports) is the one these notes were written against. Use the port labelled **USB** (the chip's native USB), not **UART** or **COM**.

## First flash of a fresh board

A fresh devkit runs a factory program that presents a *software* USB-serial port ("Espressif Device", product id `0x4001`). esptool cannot auto-reset through it, so the first flash needs download mode by hand: **hold BOOT, tap RST, release BOOT.** The board then re-enumerates as the chip's hardware "USB JTAG/serial debug unit" (product id `0x1001`) and `esphome upload` works, including the automatic reset, from then on. The browser flasher tells first-time users the same thing.

## Reading logs over USB on macOS: the trap

The S3's USB device **drops off the bus during every reset**, and the DTR/RTS state the host leaves on the port decides the boot mode: both lines deasserted means BOOT low, so the chip comes up in the ROM downloader and stays silent. A serial monitor that pre-sets DTR/RTS low before opening (a common "don't reset the board" habit) therefore parks the board in download mode, and any open handle goes stale across the reset.

Recipe that works (pyserial):

1. Open the port with pyserial defaults (DTR and RTS asserted; that is the idle state, it does not reset).
2. To reboot into the app: set `dtr=False, rts=True`, hold 300 ms, then `dtr=True, rts=True`.
3. Close, then **reopen** the port (it re-enumerated). Read.

`esphome logs` over USB shows nothing after a reset for the same reason; prefer logs over the network (`esphome logs <yaml> --device <ip>`) once the board is on Wi-Fi.

The browser flasher (ESP Web Tools) hits the same trap: its reset after a successful install leaves a board that was put into download mode by hand parked in the downloader (no Improv answer, the RGB LED keeps its old colour, no `amble-home` network). A reset or a power cycle brings the firmware up; the page tells people to do that. Improv over the native USB port then works.

## Two boxes on one network

Every box is `amble-home-xxxxxx` (the last six digits of its hardware address); `amble-home.local` is an alias that any box up will answer, so with two boxes up address them by full name or IP (`python3 -c "import socket; print(socket.getaddrinfo('amble-home-xxxxxx.local', 80)[0][4][0])"`; `dns-sd` never exits, and the lookup can take five seconds, so give `curl` a longer timeout).

Bench work: `esphome run amble-home.dev.yaml --device <ip>` over the air (or `esphome upload ... --device /dev/cu.usbmodemNNN` over USB, then a reset: the trap above), logs over the network (`esphome logs amble-home.dev.yaml --device <ip>`), and the light driven over the native API (`aioesphomeapi`; the dev build's API has no encryption key) or from Home Assistant if the board is in it. The spare fixture stays off at the end of a session.

## A production box beside a dev board

If you run amble-home in production and develop on a second board, the production box takes new builds only as a deliberate update over ESPHome's OTA from the CI image, after the dev board has run that image: download the `amble-home-firmware` artifact of the green `main` run (`gh run download <run-id> -n amble-home-firmware -D <dir>`), push it to the dev board first, then `esphome upload amble-home.yaml --device <production-ip> --file <dir>/amble-home-esp32s3.ota.bin`; the upload takes about 25 seconds, the box is back on the mesh within another 15, and `GET /amble/status` must show every fixture live before the update counts. Keep the previous image on disk until then.

A release image has no Wi-Fi credentials of its own and the dev build never saved its compiled-in ones, so after that OTA a dev board comes up with no network: send the bench credentials over Improv on the USB port, then it is back at its address with its fixtures and saved sign-in intact. Return it to the dev config over the air afterwards.

## Erasing the box

A full erase is the documented recovery for a box whose mesh safety record (`identity.h`, NVS key `ident`) cannot be read or is full, and the only way to forget mesh identities. The erase also discards the address seed (NVS key `seed`), so the box comes back at a new random address; there is a 1 in 16384 chance per earlier identity that it lands on an address it used before with a restarted sequence number, in which case the fixtures may ignore it; the remedy is to erase and flash once more. From the Mac with the box on USB: `esptool --port /dev/cu.usbmodemNNN --chip esp32s3 erase-flash`, then `esphome upload <yaml> --device /dev/cu.usbmodemNNN`, then a reset. The dev build comes up on the bench Wi-Fi by itself; the release build needs its credentials again (Improv over the same USB port, or the flasher page's **Connect to Wi-Fi**). The Home Assistant entry survives (anchored to the box's hardware address).

## Wi-Fi and secrets

The bench network's SSID and password live in a gitignored `secrets.yaml` next to the YAML that uses them (`!secret wifi_ssid`, `!secret wifi_password`), along with the spare fixture's mesh address (`!secret spare_address`). `secrets.yaml.example` shows the shape. Never paste any of it into an issue or a log excerpt. If the board lands on a separate IoT VLAN, ESPHome's API (6053), web (80) and OTA (3232) ports all need to be reachable across it.

## Driving a spike from the Mac: REST, not Home Assistant

ESPHome's `web_server` exposes every entity over HTTP. Paths use the **URL-encoded entity name**, not the YAML id: `GET /sensor/Mesh%20sequence%20number`, `POST /switch/P60c%20power/turn_on`, `POST /number/P60c%20brightness/set?value=60`, `POST /button/Reboot/press`. `GET /events` streams every id. Template sensors refresh on their `update_interval`, so counters can lag a few seconds. `spikes/02-mesh/drive.py` wraps this.

## The box's own pages

`http://amble-home.local/` is the Home (status) page; `/amble/edit` is the Edit page (choice page, then the account door or the database door: tick fixtures, ID, Save). Pages and sql.js live in `web/box/`; run `node web/box/build.mjs` before `esphome compile` (the component refuses to build without `web/box/dist/`; `cd web/importer && npm install` first if sql.js is missing). The PIN set on the Edit page gates `POST /amble/import`, `/amble/identify`, `/amble/pin`, and the cloud routes `POST`/`GET /amble/cloud/login` and `POST /amble/cloud/forget` via the `X-Amble-Pin` header; `0000` removes it; five wrong tries lock it for 30 s. Status JSON for scripts: `GET /amble/status`. `node tools/edit-page-flow.mjs http://amble-home.local <amaran.db>` replays the Edit page's Save in headless Chrome and reports whether it returned to Home (`--script <file>` substitutes an unflashed `box.js`). The Edit page's account door needs the board on a network with internet (the box makes amaran's sign-in call; its setup Wi-Fi has none and the door says so) and the browser able to reach both the box and amaran (the browser fetches the mesh). `node tools/door-check.mjs` drives all three doors in headless Chrome against the built `web/box/dist/` with scripted stand-ins for the box's cloud routes and for amaran: no box, no amaran, no credentials; it is the check for the door's wiring (the password leaves the page once, in the POST to the box, with the field already empty; failure copy; picker; PIN prompt; Remember, Refresh and Forget; a stale fetch never overwriting a database load). The box-to-amaran leg is checked from the bench with a made-up account (`POST /amble/cloud/login`, then poll the `GET`: amaran answers code 20014 for an account that does not exist, and the log line reads `cloud login failed: stage=code status=200 code=20014 (internal heap A -> B)`). A real sign-in is done by hand on the dev board, never scripted.

## The native-USB port can stall the firmware

While a host holds the USB-JTAG serial port open without reading it (esptool waiting for a bootloader that never comes, a monitor that isn't draining), ESPHome's main loop stops (Wi-Fi, API and REST go quiet) until the host lets go; the Bluetooth task keeps running. Seen three times in one evening, about 90 s each. The product must not depend on USB logging being harmless.

## Resetting the chip from the Mac is unreliable while the firmware runs

The DTR/RTS recipe above worked once and failed later; esptool's `default-reset` and `usb-reset` both failed to enter the bootloader while the firmware was running. Don't burn time on it: **tap the board's reset, or unplug and replug the cable** for a power cycle, or use the firmware's own restart button over REST for a soft reboot.

## Two configs: dev on the bench, release everywhere else

`amble-home.dev.yaml` is the bench config: Wi-Fi from `secrets.yaml`, `release: false`, the spare's address in `dev_allowlist`, its own `build_path`. `amble-home.yaml` is the release config strangers flash: no credentials, `release: true` (the guard may address any fixture in the box's own table). **Never flash the release config to the bench board while its table could hold a production fixture**; verify it by compiling only. Two ESPHome configs with the same `name` and no `build_path` share a build directory and silently overwrite each other's binaries.

## Toolchain locations

ESPHome lives in a project `.venv` (Python 3.13; `uv` makes one quickly). ESPHome's native IDF install and toolchains are under `~/Library/Caches/esphome/idf/` on macOS (`frameworks/<version>` is the IDF source tree). There is no PlatformIO.
