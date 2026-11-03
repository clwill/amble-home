#!/usr/bin/env node
// Validates a rendered flasher manifest: node web/flasher/check.mjs <manifest.json> [--firmware-dir <dir>]
// Exits non-zero on any mismatch, so CI never publishes a manifest that points nowhere.
// The manifest carries one build per chip family the flasher offers (#24): ESP32-S3
// and ESP32-C6, each a single factory image at offset 0 next to the manifest. ESP Web
// Tools picks the build from the chip it finds on the port.
import { readFileSync, statSync } from "node:fs";
import { join } from "node:path";

export const CHIP_FAMILIES = ["ESP32-S3", "ESP32-C6"];
export const MIN_IMAGE_BYTES = 1_000_000;

// Returns the list of problems with a parsed manifest (empty when it is good).
// `dir`, when given, is where the images must exist.
export function problems(m, dir = null) {
  const errors = [];
  if (m.name !== "amble-home") errors.push("name must be amble-home");
  if (typeof m.version !== "string" || !m.version || m.version.includes("__")) errors.push("version not rendered");
  if (m.home_assistant_domain !== "esphome") errors.push("home_assistant_domain must be esphome");
  const builds = Array.isArray(m.builds) ? m.builds : [];
  const families = builds.map((b) => b?.chipFamily);
  for (const fam of CHIP_FAMILIES) if (!families.includes(fam)) errors.push(`no build for ${fam}`);
  for (const fam of families) if (!CHIP_FAMILIES.includes(fam)) errors.push(`unexpected chipFamily ${fam}`);
  if (new Set(families).size !== families.length) errors.push("a chip family appears twice");
  const paths = [];
  for (const b of builds) {
    const fam = b?.chipFamily ?? "?";
    if (!Array.isArray(b?.parts) || b.parts.length !== 1) { errors.push(`${fam}: exactly one part (the factory image) expected`); continue; }
    const p = b.parts[0] ?? {};
    if (p.offset !== 0) errors.push(`${fam}: factory image must be at offset 0`);
    if (typeof p.path !== "string" || !p.path.endsWith(".factory.bin") || p.path.includes("/")) { errors.push(`${fam}: part path must be a bare *.factory.bin next to the manifest`); continue; }
    paths.push(p.path);
    if (dir) {
      try {
        const size = statSync(join(dir, p.path)).size;
        if (size < MIN_IMAGE_BYTES) errors.push(`${fam}: firmware ${p.path} is only ${size} bytes`);
      } catch { errors.push(`${fam}: firmware ${p.path} not found in ${dir}`); }
    }
  }
  if (new Set(paths).size !== paths.length) errors.push("two builds point at the same image");
  return errors;
}

if (process.argv[1] && import.meta.url === new URL(`file://${process.argv[1]}`).href) {
  const [path, ...rest] = process.argv.slice(2);
  if (!path) { console.error("usage: check.mjs <manifest.json> [--firmware-dir <dir>]"); process.exit(2); }
  const dirIdx = rest.indexOf("--firmware-dir");
  const dir = dirIdx >= 0 ? rest[dirIdx + 1] : null;
  let m;
  try { m = JSON.parse(readFileSync(path, "utf-8")); } catch { console.error("manifest is not valid JSON"); process.exit(1); }
  const errors = problems(m, dir);
  if (errors.length) { for (const e of errors) console.error("manifest: " + e); process.exit(1); }
  console.log(`manifest ok: ${m.name} ${m.version} (${m.builds.map((b) => b.chipFamily).join(", ")})`);
}
