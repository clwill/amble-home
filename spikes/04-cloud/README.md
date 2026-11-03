# Spike 4: the amaran cloud, captured once

**Question.** What does the `amble_cloud` door have to send to get a user's mesh keys and fixture table from the amaran cloud, so a phone-only (iOS) user can import without a database file?

**Answer (2026-10-28, Chris's own account, read-only, nothing but shapes kept).** Two calls, plain JSON over HTTPS, no request signing:

1. `POST https://us.sidus.link/uc/user/login` with body `{"email", "password", "area"}` (`area` is the two-letter region, `US`; the password goes as typed, over TLS, not hashed). Reply `{"code": 20000, "data": {"id", "token", "expire_time", "user_info", "auth_string", "device_list"}}`. Every later call carries that `token` as a request header. The app also sends descriptive headers (`platform`, `request_source`, `product_version`, `device_ui_type`, `device_language`, `device_name`, `phone_model`, `device_identifier`); the server accepted made-up values.
2. `GET https://us.sidus.link/amaran/pack/query?mesh_update_time=0&fixture_update_time=0&...` (every `*_update_time` cursor at 0 means "everything"; the app sends its last sync times to get only changes). Reply `data.meshs.existing[]` with `mesh_uuid`, `net_key`, `app_key`, `fixtures_ordered_list`, and `data.fixtures.existing[]` with `mesh_uuid`, `node_address`, `mac_address`, `code` (the model code our fixture matrix keys on), `node_name`, `name`, `composition_data`, `device_uuid`, `device_key`, `binding`, `fast_provision_supported`, `platform`, `update_platform`. Groups come too (`node_address` 0xC000.. with members). No IV index in the record; the importer already defaults it to 0 when the database lacks the column.

That is exactly what the database import produces (`web/importer/importer.js`: `net_key`, `app_key`, and per fixture `mac`, `address`, `name`, `code`), so the cloud door is a second source for the same payload, built in the user's browser or on the box.

Regions: `us`, `eu`, `cn`, `gs` (global services, e.g. `/potap/...` fixture-config updates). `GET /amaran/mesh/importableV2` lists meshes importable from other platforms; it was empty here and is not needed. Response `code` 20000 is success; 20003 came back for a refresh-token call with a stale token.

**How it was measured.** The Desktop app (v1.1.03, Nuitka-compiled Python) uses a bare `urllib3` pool with its own `certifi` bundle, so it ignores proxies and the system CA. The capture therefore put the two cloud names into `/etc/hosts`, ran mitmdump in reverse mode on 443 (`reverse_addon.py`: forwards by real IP, keeps the Host header and SNI) with a recording addon (`capture_addon.py`) that writes only method, path, parameter names, value types and lengths, dropping anything whose key looks like a key, token, password or MAC; the proxy's CA was appended to the app's bundle for the hour. The app's own sign-in gave the login format; `cloud-replay.py` then made the two calls with cursors at zero (the app only asks for changes since its last sync). `shapes.jsonl` is the replay's record. Everything was reverted the same morning (hosts lines removed, CA file restored, helpers deleted).

**Also found, not our path.** amaran Desktop serves a local Open API over WebSocket (`ws://127.0.0.1:33782/ws`, JSON requests with an AES-GCM "token"; documented at tools.sidus.link/openapi; used by Bitfocus Companion and `jtannahill/amaran-mcp`). It controls fixtures while the app runs on a computer and exposes no keys.

Throwaway code; never product code.
