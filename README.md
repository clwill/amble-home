# amble-home

**Amaran BLE for ESPHome.** Put your amaran (Aputure) Bluetooth-mesh studio lights into Home Assistant as real devices, on a $13 ESP32 board. From a bare board in its plastic bag to lights in Home Assistant takes under ten minutes, all from your browser, with nothing to install on a computer.

- **Flash it from your browser.** Plug the board in, press Install, join your Wi-Fi from the same page, then open the amble-home box with one click.
- **Import your fixtures** by signing in to your amaran account on the amble-home box's own page, or by handing it the amaran desktop app's database. Your mesh keys travel from your browser to the amble-home box on your own network and nowhere else.
- **Lights appear in Home Assistant** with on/off, brightness, color temperature, color, and green/magenta where the fixture has it. Turning a light on brings it back exactly as it was.
- **Keep using the amaran app.** The amble-home box is one more node on your mesh, not a replacement for the app. Home Assistant follows what you do there within seconds.
- **A standard ESPHome device.** The amble-home box is discovered on its own, updated over the air, with diagnostics on its device page. Nothing runs on a computer afterwards.
- **And once a light is in Home Assistant** you can run it from Alexa, Google Home, or Siri like any other light. The [Home Assistant](https://github.com/clwill/amble-home/wiki/Home-Assistant) page says where to switch that on.

**Status:** v1.0.0. amble-home has run the author's studio, eight lights through Home Assistant, since 2026-10-29.

## Why this exists

The amaran app is great at what it does but controlling the lights outside of it doesn't work. The lights speak a Bluetooth mesh that [wesbos](https://github.com/wesbos/amaran-BLE-control) reverse-engineered, and a hand-built ESP32 bridge grew out of that work and ran the author's studio over MQTT for months. It worked, but everything in it besides the mesh (Wi-Fi, HTTP, discovery, logging, and no over-the-air updates) is work ESPHome already does better and keeps doing better without anyone maintaining it. amble-home is that bridge rebuilt as one ESPHome component, so the mesh knowledge is the only thing it has to own, and everything a stranger needs (flash, Wi-Fi, updates, Home Assistant) comes from ESPHome.

## What you need

- An **ESP32-S3 or ESP32-C6 board** with 8 MB of flash and a native USB port. The [Boards](https://github.com/clwill/amble-home/wiki/Boards) page lists the five that have run the whole path, what else should work, and what will not. The classic ESP32 is not supported.
- Your **amaran account**, the one your lights are registered to, or the amaran desktop app signed in to it. The [Lights](https://github.com/clwill/amble-home/wiki/Lights) page says which fixtures are known to work.
- **Home Assistant** with the ESPHome integration, which is built in.
- A computer with **Chrome or Edge** for the one-time flash. Everything after that works from any browser or phone.

## Ten minutes, start to finish

1. Open the [flasher page](https://clwill.github.io/amble-home/), plug the board in, put it in install mode, press **Connect**, then **Install**.
2. Tap the board's reset, press **Connect** again, choose **Connect to Wi-Fi**, then **Visit Device**.
3. On the box's page press **Edit**, sign in to your amaran account (or pick your `amaran.db`), check the fixtures this box should control, press **Save**.
4. Home Assistant offers the new device under **Settings → Devices & services**. Press **Configure**. Your lights are there.

The [Installing](https://github.com/clwill/amble-home/wiki/Installing) page walks each step with the buttons and ports for every board, and [Troubleshooting](https://github.com/clwill/amble-home/wiki/Troubleshooting) covers the things that go wrong and how to report the rest.

## The wiki

Everything beyond this page lives in the [wiki](https://github.com/clwill/amble-home/wiki): [Installing](https://github.com/clwill/amble-home/wiki/Installing), [Boards](https://github.com/clwill/amble-home/wiki/Boards), [Lights](https://github.com/clwill/amble-home/wiki/Lights), [Importing](https://github.com/clwill/amble-home/wiki/Importing), [Home Assistant](https://github.com/clwill/amble-home/wiki/Home-Assistant), [Updating](https://github.com/clwill/amble-home/wiki/Updating), [Troubleshooting](https://github.com/clwill/amble-home/wiki/Troubleshooting), [Developers](https://github.com/clwill/amble-home/wiki/Developers) and [Lineage](https://github.com/clwill/amble-home/wiki/Lineage).

## Limits, stated plainly

- Flashing is a desktop act (Web Serial). Everything after that works from any browser or phone.
- The box never controls lights from its own pages; Home Assistant is the only place that happens.
- The box talks to your fixtures one at a time: group and broadcast mesh commands are compiled out of public builds.
- Your amaran password goes to the box over your home network, is used for one sign-in, and is not stored unless you check *Remember*. Then the box keeps it in plain storage next to your Wi-Fi password until you press *Forget*. The box never signs in on its own.
- The amaran app stays the source of truth for the mesh. Add, remove or rename lights there, then re-import.

## Developing

Everything lives in one ESPHome external component, `components/amble/`, plus the box's pages and the flasher page under `web/`. Build the page assets once with `node web/box/build.mjs`, then `esphome compile amble-home.yaml`. Tests: `bash tests/host/run.sh`, `node --test web/box/box.test.mjs` and `cd web/importer && node --test`. The [Developers](https://github.com/clwill/amble-home/wiki/Developers) page explains how it works and how to contribute; the working rules, including who decides what, are in `AGENTS.md`; rationale in `docs/decisions.md`; the bench procedure in `docs/bench.md`.

## Credits and lineage

- [wesbos/amaran-BLE-control](https://github.com/wesbos/amaran-BLE-control) (MIT): the reverse engineering of the amaran mesh, the discovery that the keys live in the desktop app's database, and the first direct control. Everything here stands on that.
- The author's earlier studio bridge, `amaran-BLE-control` on branch `bridge-local`: the ESP-IDF firmware that ran seven lights in production and taught us the IDF patches, the status snoop and the stability tripwires. Its lessons are in `docs/origin.md`.
- [abrockmann/amaran-BLE-control-streamdeck](https://github.com/abrockmann/amaran-BLE-control-streamdeck): Arne Brockmann's sibling fork for the Stream Deck, where the mesh advertising-pool race was found and fixed together. amble-home is the Home Assistant branch of that family tree, not a competitor to it.
- [ESPHome](https://esphome.io) and [ESP Web Tools](https://esphome.github.io/esp-web-tools/): the platform and the flasher this rides on.

The [Lineage](https://github.com/clwill/amble-home/wiki/Lineage) page tells the longer story.

## About this project

The repository is owned and directed by Chris Williams. The software is written and maintained by Claude (Anthropic), working as the project's engineer under Chris's direction, with Codex (OpenAI) reviewing the slices that touch keys, the mesh, the import page or the release pipeline. The working rules, including who decides what, are in `AGENTS.md`.

MIT licensed.
