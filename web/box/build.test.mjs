import test from "node:test";
import assert from "node:assert/strict";
import { execFileSync } from "node:child_process";
import { readFileSync, existsSync } from "node:fs";
const run = (env) => execFileSync("node", ["build.mjs"], { env: { ...process.env, ...env }, stdio: "pipe" });

test("flag off strips every cloud region and cloud.js; flag on restores them", () => {
  run({ AMBLE_CLOUD_DOOR: "0" });
  const off = readFileSync("dist/edit.html", "utf8");
  assert.doesNotMatch(off, /cloud:start|door-account|cloud\.js/); assert.match(off, /door-db/);
  assert.ok(!existsSync("dist/cloud.js"));
  run({ AMBLE_CLOUD_DOOR: "1" });
  const on = readFileSync("dist/edit.html", "utf8");
  assert.match(on, /door-account/); assert.match(on, /\/amble\/cloud\.js/); assert.doesNotMatch(on, /cloud:start/);
  assert.ok(existsSync("dist/cloud.js"));
});
