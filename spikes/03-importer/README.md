# Spike 3 — in-browser `amaran.db` importer (throwaway)

> The logic graduated to `web/importer/` (product code, with `node --test` tests);
> the files here import it from there and stay as the record of the spike.

Question: can a static page open the amaran Desktop app's SQLite
database with sql.js, list the fixtures with their mesh addresses and
capabilities, and confirm the keys are present — without the file or the keys
ever leaving the browser?

Nothing here is product code; the logic in `importer.js` is the thing the real
Edit page's database door ports.

## Files

- `importer.js` — the import logic, shared by the page and the Node harness.
  Port of the studio bridge's `scripts/generate-config.ts`: the SQL, the
  `fixture_<code>_<hw>` capability lookup, the fallback table and defaults.
  Returns key **presence**, never values (a `withKeys` flag exists for the
  box-side payload later; the spike never sets it).
- `importer.html` — file picker + sql.js (cdnjs, pinned 1.15.0, same as the
  harness) + a table. Database errors are replaced by fixed messages so no SQLite
  text can reach the page.
  MACs are shown with the first three bytes masked. `?db=<url>` is a spike-only
  hook so a headless browser can drive it.
- `fixture-caps.json` — the capability fields amble-home needs, derived from
  the amaran Desktop app's `fixture_config.json` (97 fixture entries, 80 model
  codes): color, green/magenta, CCT range, per hardware version.
- `test.mjs` — Node harness (`npm install && npm test`) that runs the same logic
  against `./amaran.db` and prints **fixed diagnostics only**: counts, key
  presence, capability-source tallies and structural checks, then PASS/FAIL.
  Nothing derived from the database (names, addresses, MACs, model codes, keys)
  is printed — that table is for the browser page, not the terminal.
- `amaran.db` — **not committed** (gitignored `*.db`). Chris copies it out of the
  app's sandbox container; see `docs/bench.md`.

## Findings (2026-10-25)

Schema `a62d9e4b8f10` (alembic); `mesh` has
`net_key`/`app_key` as 32-hex strings and **no `iv_index` column** — the
firmware learns the IV index from the secure network beacon. Capability mapping
agrees with the bridge's generated `mesh_config.h` for all seven studio fixtures.
