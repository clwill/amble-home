# Spike 2 — mesh hello and coexistence (throwaway)

Questions: does key injection work on ESPHome's pinned IDF; can a
node of our own on the amaran mesh hear fixture status replies without an IDF
patch (linker-wrap snoop); can it toggle the spare P60c by unicast; do the
sequence number and IV index survive reboot and power loss.

Nothing here is product code; `components/amble_spike2/` is the shape the
real `amble_mesh` component takes.

## Files

- `gen-mesh-config.mjs` — reads the gitignored `amaran.db` copy (Spike 3's
  importer logic, with keys) and writes `components/amble_spike2/mesh_config.h`:
  NetKey, AppKey, the P60c's unicast address. **Key-bearing, gitignored.** Run
  `node gen-mesh-config.mjs` before compiling.
- `components/amble_spike2/` — the component: self-provisions as its own node
  (`own_address`, default `0x00F1`, well clear of the fixture block) with `bt_mesh_pre_provision` +
  `bt_mesh_provision`, injects the AppKey, keeps relay/beacon/proxy **off**;
  snoops every ADV-bearer PDU behind `--wrap=bt_mesh_generic_net_recv` by
  decoding a copy with the IDF's exported crypto and the stack's own keys;
  decodes Telink `0x26` status replies (`telink.cpp`, ported from the bridge).
  With `transmit: true` it sends Telink commands by unicast to the one
  hard-wired target and nothing else — a `static_assert` refuses a non-unicast
  target and there is no code path to any other destination.
- `spike2-s3.yaml` — the board config plus a REST-drivable test harness:
  switch/number/button entities for power, brightness, CCT, hue and status
  request; sensors for snooped replies, commands sent, mesh sequence number and
  IV index; a restart button. `substitutions: transmit` flips phase A
  (receive-only) to phase B.

## Phases

- **A, receive-only.** Keys in, node up, nothing sent. The production bridge
  polls every fixture, so status replies are on the air; the snoop must decode
  them. Evidence: `status 0x00xx on CCT …` lines in the log, counters rising.
- **B, unicast to the P60c.** Toggle, dim, CCT, hue; confirm by the P60c's own
  status replies. Then reboot (soft) and hard-reset (USB EN pulse, the bench
  stand-in for power loss) and check the sequence number never goes backwards
  and the fixture still obeys.

## Dev guard

The one target is the bench P60c, whose address lives only in the gitignored
`mesh_config.h` (generated from the database). Group `0xC000` and broadcast
`0xFFFF` are never sent; see `send_telink_` in `amble_spike2.cpp`. Logs label
fixtures `target`/`other` and never print mesh addresses (standing rule 2).

## Result (2026-10-25)

Phase A and B both passed on the bench; two soft reboots restored the sequence
number from NVS; 20/20 polls answered. **Not done:** a true power cycle — three
USB-driven chip resets failed (see `docs/bench.md`), so that one is Chris's
unplug/replug tomorrow. Evidence in `docs/changelog.md`.
