---
name: verify-surfaces
description: Use when choosing verification for an amble-home change, preparing a handoff, commit or push, or checking whether touched surfaces are ready.
---

# Verify touched surfaces

Inspect the complete diff first. Run checks proportional to what changed.

## Matrix (grows as the project is built)

- Agent instructions, skills or shims: `node scripts/check-ai-tooling.mjs`.
- ESPHome config or the external component: `esphome config <yaml>` must pass; `esphome compile <yaml>` for any C++ change; flash the **dev board** and show a fixture responding for any mesh-layer change (log excerpt + HA state read-back on the issue). Never flash the production bridge to verify.
- IDF patches: the patch applies cleanly to the pinned IDF version and the firmware compiles without warnings.
- Importer/flasher web page: build passes; a run against a copy of `amaran.db` with the resulting device config read back from the device. For the box's Edit page, `node tools/edit-page-flow.mjs http://amble-home.local <amaran.db>` drives a real Chrome through Save and must land on Home (bench box only: it reboots).
- Documentation only: inspect the rendered text; deterministic checks tied to its format.

Never push, release, flash production or send group/broadcast mesh commands merely to verify. Those actions need their own authorization.
