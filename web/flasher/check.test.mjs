// node --test web/flasher/  — the manifest template renders to something the checker
// accepts, and the checker refuses the shapes that would strand a board.
import { test } from "node:test";
import assert from "node:assert/strict";
import { readFileSync, mkdtempSync, writeFileSync } from "node:fs";
import { tmpdir } from "node:os";
import { join } from "node:path";
import { execFileSync, spawnSync } from "node:child_process";
import { problems, CHIP_FAMILIES, MIN_IMAGE_BYTES } from "./check.mjs";

const here = new URL(".", import.meta.url).pathname;
const render = (version = "0.0.0+abc1234") =>
  JSON.parse(readFileSync(join(here, "manifest.template.json"), "utf-8").replace("__VERSION__", version));

// A directory holding every image the rendered template names, each big enough.
function imagesDir(m, { drop = null, bytes = MIN_IMAGE_BYTES } = {}) {
  const dir = mkdtempSync(join(tmpdir(), "amble-manifest-"));
  for (const b of m.builds) if (b.chipFamily !== drop) writeFileSync(join(dir, b.parts[0].path), Buffer.alloc(bytes));
  return dir;
}

test("the template renders to a manifest with one build per chip family, all images present", () => {
  const m = render();
  assert.deepEqual(m.builds.map((b) => b.chipFamily).sort(), [...CHIP_FAMILIES].sort());
  assert.deepEqual(problems(m, imagesDir(m)), []);
});

test("an unrendered version is refused", () => {
  assert.ok(problems(render("__VERSION__")).some((e) => e.includes("version")));
});

for (const fam of CHIP_FAMILIES) {
  test(`a manifest missing the ${fam} build is refused`, () => {
    const m = render();
    m.builds = m.builds.filter((b) => b.chipFamily !== fam);
    assert.ok(problems(m).some((e) => e === `no build for ${fam}`));
  });
}

test("a chip family the flasher does not offer is refused", () => {
  const m = render();
  m.builds.push({ chipFamily: "ESP32", parts: [{ path: "amble-home-esp32.factory.bin", offset: 0 }] });
  assert.ok(problems(m).some((e) => e === "unexpected chipFamily ESP32"));
});

test("two builds pointing at one image are refused", () => {
  const m = render();
  m.builds[1].parts[0].path = m.builds[0].parts[0].path;
  assert.ok(problems(m).some((e) => e.includes("same image")));
});

test("a missing or tiny image is named with its chip family", () => {
  const m = render();
  const missing = problems(m, imagesDir(m, { drop: "ESP32-C6" }));
  assert.ok(missing.some((e) => e.startsWith("ESP32-C6:") && e.includes("not found")));
  const tiny = problems(m, imagesDir(m, { bytes: 10 }));
  assert.equal(tiny.filter((e) => e.includes("is only 10 bytes")).length, 2);
});

test("the command line exits 0 on a good manifest and 1 on a bad one", () => {
  const m = render();
  const dir = imagesDir(m);
  writeFileSync(join(dir, "manifest.json"), JSON.stringify(m));
  const out = execFileSync(process.execPath, [join(here, "check.mjs"), join(dir, "manifest.json"), "--firmware-dir", dir], { encoding: "utf-8" });
  assert.match(out, /manifest ok: amble-home 0\.0\.0\+abc1234 \(ESP32-S3, ESP32-C6\)/);
  const bad = spawnSync(process.execPath, [join(here, "check.mjs"), join(dir, "manifest.json"), "--firmware-dir", imagesDir(m, { drop: "ESP32-S3" })], { encoding: "utf-8" });
  assert.equal(bad.status, 1);
  assert.match(bad.stderr, /ESP32-S3: firmware .* not found/);
});
