# Origin: what amble-home inherits from the studio bridge

amble-home is a port, not a rewrite. The mesh knowledge comes from two places, both MIT:

- **wesbos/amaran-BLE-control** (upstream): reverse-engineered the amaran app (Telink SigMesh), found the keys in the Desktop app's SQLite database, and got direct control working. `DIRECT-BLE-CONTROL.md` there is the research record.
- **clwill/amaran-BLE-control, branch `bridge-local`** (the studio bridge, a private repository): the ESP-IDF v5.3.2 firmware that ran the author's seven fixtures from mid-2026 until amble-home replaced it and absorbed twelve local edits plus two IDF patches. Arne Brockmann's `abrockmann/amaran-BLE-control-streamdeck` is a sibling fork aimed at Stream Deck; the adv-pool race fix was contributed there (its second pull request, merged 2026-10-24).

## How the bridge talks to the lights (the part that carries over)

1. **It is not provisioned by the app and the app does not know it exists.** Keys (mesh NetKey, AppKey), the IV index and the fixture table (MAC, unicast address, name, model code → capabilities) are read out of the amaran Desktop app's `amaran.db` on the Mac (`scripts/generate-config.ts`) and compiled into `main/mesh_config.h`. Fixture capabilities (colour, green/magenta, CCT range) come from the app bundle's `fixture_config.json`, keyed by model code, with fallbacks.
2. **Key injection uses internal IDF calls, not the public API.** After `esp_ble_mesh_node_prov_enable`, the firmware calls `bt_mesh_provision` (IDF 5.3; split into `bt_mesh_pre_provision` + `bt_mesh_provision` in 5.5) with the imported NetKey, the IV index and a random DeviceKey, then `bt_mesh_node_local_app_key_add` and `bt_mesh_node_bind_app_key_to_model` to install the AppKey and bind it to a vendor model. **The bridge is its own mesh node at unicast `0x00F0`** (`ESP32_UNICAST_ADDR` in `main.c`, moved from `0x0010` after a collision with a fixture), and it transmits from that address through `esp_ble_mesh_server_model_send_msg`. It does *not* send as the provisioner `0x0001`; `AMARAN_LOCAL_SRC 0x0001` is defined in `mesh_config.h` but unused. Corrected 2026-10-25 from the source; the earlier wording here (and the v1 spec's coexistence framing) assumed a shared `0x0001` identity. The box has always had its own identity and sequence space; what it shares with the app is only the NetKey/AppKey.
3. **Commands are Telink vendor-model opcodes** (`telink.c`, 155 lines): on/off, brightness 0–1000, CCT (Kelvin + intensity), HSI (hue, saturation, intensity), status request; status decode. Fixtures reply to `0x0001`.
4. **Status feedback needs IDF patch 0001.** Fixture status replies are addressed to the provisioner unicast, which the IDF access layer never delivers to our models. `patches/0001-amaran-net-recv-status-snoop.patch` decrypts a copy of the access payload in `bt_mesh_net_recv` (IDF 5.3; `bt_mesh_generic_net_recv` in 5.5) and hands it to a weak hook. Needed because the fixtures reply to the provisioner `0x0001`, not to the bridge's own `0x00F0`, so the reply is never "for us". There is no public callback for this; the patch is unavoidable as long as we snoop.
5. **Stability needs IDF patch 0002.** ESP-IDF v5.3.2's mesh adv buffer pool has an alloc/free counting race on dual-core parts that shows up as a slow "leak" (`-ENOBUFS` after ~6.7 days). Upstream fix is esp-idf commit `87c0b28459` ("feat(ble_mesh): add adv lock to avoid allocated buffers"); `patches/0002-amaran-adv-alloc-lock.patch` is its backport. Arne found a residual gap (count touched outside the lock in `net_buf_unref` and `net_buf_alloc_len`), rare, upstream has it too. A ghost-count tripwire (`adv_buf_ghost`, must stay 0) is the detection.
6. **Mesh stack config** (`sdkconfig.defaults`): Bluedroid host, BLE 4.2 features, `BLE_MESH_NODE`, PB-ADV + PB-GATT, GATT proxy server, relay on, friend/LPN off, 1 subnet, 2 app keys, CRPL 20.
7. **Footprint.** The bridge runs a single 3 MB factory partition on 4 MB flash with **no OTA**. Its own comment: mesh stack ~1 MB, whole app close to 2 MB with Wi-Fi + MQTT + HTTP + mDNS. ESPHome replaces MQTT/HTTP/mDNS with its own, so the real number for amble-home is unknown until measured. This is the first spike.

## The twelve edits, and what happens to each

| Bridge edit | Fate in amble-home |
|---|---|
| Edits 1–10: diagnostics + MQTT discovery layer (`DIAG_SENSORS`, tx diag, incident capture) | **Replaced** by ESPHome diagnostic sensors on the native API. Keep the *signals*: adv pool free count, ghost count, mesh send completions, RSSI. |
| Edit 7: strongest-AP Wi-Fi selection | **Dropped.** ESPHome `wifi:` has `fast_connect`/roaming options; the operator's real fix was a dedicated in-room SSID. |
| Edit 8: unicast clear of fixture addresses | **Carried** into the mesh component (bug fix in the vendor-model send path; check whether it survives the key-injection redesign). |
| Edit 9: on/off verify + retry | **Carried** as component behaviour: a command without a matching status within a timeout retries once, then marks the entity unavailable-ish (exact HA semantics in the spec). |
| Edit 11: adv alloc lock (IDF patch 0002) | **Carried** as a patch until the pinned IDF contains the upstream fix; then dropped. |
| Edit 12: start MQTT/HTTP once, not per got-IP | **Dropped.** ESPHome owns network lifecycle. |

## Facts worth not relearning

- ESP32s pick the first BSSID in channel order, not the strongest. Never fix a wrong-AP ESP32 with min-RSSI. The author's fix was a dedicated SSID served by the one access point in the room.
- The amaran Desktop app exists for macOS and Windows. The `amaran.db` path on macOS is `~/Library/Application Support/amaran Desktop/*/amaran.db`. iOS/iPadOS is the product's main surface and its database is not readable without a jailbreak; the phone-only story is an open design question.
- Group `0xC000` and broadcast `0xFFFF` reach every fixture on the mesh. The dev build must not send them.
- Protocol facts (opcodes, scaling, HSI vs CCT exclusivity, status layout) are documented in `telink.h`/`telink.c` of the bridge and in `DIRECT-BLE-CONTROL.md` upstream. Port them, with the comments.
- IDF v5.3.2 needed both patches to *build the bridge as shipped*; ESPHome pins its own IDF (configurable via `esp32.framework.source`), so the first spike must settle which IDF amble-home builds against and whether 0001/0002 apply to it.
