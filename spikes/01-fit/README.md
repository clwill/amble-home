# Spike 1 — fit, build and OTA recovery (throwaway)

Question: does the amaran BLE-mesh stack build and fit under an
ESPHome `esp-idf` build on the ESP32-S3 N16R8, with the status-snoop patch, and
what does the app weigh against the two OTA slots?

Nothing in this directory is product code. Findings go to `docs/changelog.md`
and `docs/decisions.md`; the directory stays as the record of how they were
measured.

## Run

```sh
cd spikes/01-fit
../../.venv/bin/esphome compile spike1-s3.yaml            # the good image
../../.venv/bin/esphome compile spike1-s3-badota.yaml     # same build, aborts in setup()
```

Needs a gitignored `secrets.yaml` with two YAML lines, `wifi_ssid: "…"` and
`wifi_password: "…"`; the repo-root `.env` holds the same two lines, so `cp ../../.env secrets.yaml`
works. The project `.venv` must exist (`docs/bench.md`). The bad-OTA variant has its own `build_path`; see `docs/bench.md`
for why, and for the first-flash and USB-logging procedure on the Mac.

**Result (2026-10-25):** 1.28 MB image, 7.94 MB per OTA slot, mesh stack initialiszes
on the bench board, and the bad image pushed over OTA was rolled back by the
bootloader without intervention. Full write-up in `docs/changelog.md`.

ESPHome 2026.10 builds esp-idf natively (CMake + ninja, IDF under
`~/Library/Caches/esphome/idf/frameworks/<ver>`), not through PlatformIO, so
`platformio_options` is ignored. The first compile downloads toolchain + IDF.

## What is in here

- `spike1-s3.yaml` — product-shaped ESPHome config (Wi-Fi station + fallback AP +
  captive portal, Improv, native API, OTA + safe mode, web server, logger,
  `esp32_ble`, the board's RGB LED forced off) plus the spike component. Wi-Fi
  credentials come from the gitignored `secrets.yaml`; without it `esphome config`
  fails on a fresh clone, by design.
- `spike1-s3-badota.yaml` — the same build minus the LED block, plus
  `crash_on_boot: true`; its own `build_path`.
- `components/amble_spike/` — pulls in the bridge's mesh `sdkconfig` (plus
  `CONFIG_BT_GATTS_ENABLE`, which the mesh GATT bearers need and esp32_ble does
  not set), initialiszes the mesh stack after `esp32_ble` has Bluedroid up,
  references the key-injection symbols so the linker keeps them, and wraps
  `bt_mesh_generic_net_recv` with `-Wl,--wrap` as the candidate no-patch snoop
  point. **Never transmits**: no provisioning bearers enabled, no keys injected.
- `patches/0001-amaran-net-recv-status-snoop-idf55.patch` — the bridge's
  `net.c` status snoop re-ported from IDF 5.3.2 to 5.5.x, applies cleanly to a
  pristine 5.5.6 tree. Kept as the fallback if the linker-wrap approach fails in
  Spike 2; not applied in this build. Patch 0002 (adv-buffer lock) is not
  needed: upstream 5.5.x carries the fix.
