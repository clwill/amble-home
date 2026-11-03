#!/usr/bin/env python3
"""Build docs/fixtures.md: every amaran fixture the app knows, what amble-home maps
for it, and what has actually been tested.

Sources: the amaran Desktop app's own tables (product.json: model code -> name and
series; fixture_config.json: model code + hardware version -> capabilities). They
ship inside the app bundle and carry no user data. The TESTED table below is
maintained by hand from hardware evidence on issues; everything else is derived.

  python3 tools/fixture-matrix.py [--config <dir with product.json + fixture_config.json>] [--out docs/fixtures.md]
"""
import argparse, datetime, json, os, sys

DEFAULT_CONFIG = "/Applications/amaran Desktop.app/Contents/Resources/config"

# Hand-maintained. Keyed by model code. Each entry: status, note (issue/date).
# "tested"      = driven from Home Assistant through amble-home, with status read-back.
# "production"  = drives the author's studio daily through an amble-home box since 2026-10-27; every mapped command in daily HA use.
TESTED = {
    "40015": ("tested", "P60c: on/off, brightness, CCT, HSI, green/magenta, status read-back; milestone 2–4 acceptance, 2026-10-26"),
    "40095": ("production", "studio tubes (two); daily HA use since 2026-10-27"),
    "40025": ("production", "studio keys (two); daily HA use since 2026-10-27"),
    "400J5": ("production", "studio gobo; daily HA use since 2026-10-27"),
    "40085": ("production", "studio accent; daily HA use since 2026-10-27"),
    "400U5": ("production", "studio hair; daily HA use since 2026-10-27"),
    "400Z5": ("tested", "Verge Max: imported through the account door, then from HA on/off, brightness 1–100 %, CCT 2700/5000/6500 K, each read back; 2026-10-29"),
}
EXPECTED_SOON = {  # exact model name -> note; on order or arriving, a row people can watch
}


def flag(v):
    return str(v) in ("1", "true", "True")


def kelvin(v):
    try:
        n = int(v)
    except (TypeError, ValueError):
        return None
    return n * 100 if n < 1000 else n


def family(c):
    if c is None:
        return "unknown"
    pix = int(c.get("pixel_num") or 0)
    if flag(c.get("hsi_support")) or flag(c.get("rgb_support")):
        return "pixel colour" if pix > 1 else "colour (HSI)"
    if flag(c.get("cct_support")):
        lo, hi = kelvin(c.get("product_cct_min")), kelvin(c.get("product_cct_max"))
        return "bi-colour (CCT)" if lo != hi else "single colour"
    return "single colour"


def mapped(c):
    if c is None:
        return "on/off, brightness (capabilities unknown to the app table)"
    out = ["on/off", "brightness"]
    if flag(c.get("cct_support")):
        lo, hi = kelvin(c.get("product_cct_min")), kelvin(c.get("product_cct_max"))
        out.append(f"CCT {lo}–{hi} K" if lo and hi and lo != hi else "CCT (fixed)")
    if flag(c.get("hsi_support")):
        out.append("HSI colour")
    if flag(c.get("gm_support")):
        out.append("green/magenta")
    return ", ".join(out)


def not_mapped(c):
    if c is None:
        return ""
    skipped = []
    if any(flag(v) for k, v in c.items() if k.startswith(("systemfx", "pixelfx", "magic_pixelfx", "manualfx", "musicfx", "programfx", "pickerfx"))):
        skipped.append("effects")
    if int(c.get("pixel_num") or 0) > 1 or flag(c.get("partition_support")):
        skipped.append("per-pixel/partition control")
    if flag(c.get("xy_support")):
        skipped.append("xy colour")
    if flag(c.get("gel_support")) or flag(c.get("adv_gel_support")):
        skipped.append("gels")
    if flag(c.get("motion_support")):
        skipped.append("motion")
    return ", ".join(skipped)


def main():
    ap = argparse.ArgumentParser()
    ap.add_argument("--config", default=DEFAULT_CONFIG)
    ap.add_argument("--out", default=os.path.join(os.path.dirname(__file__), "..", "docs", "fixtures.md"))
    a = ap.parse_args()
    products = json.load(open(os.path.join(a.config, "product.json")))
    caps = json.load(open(os.path.join(a.config, "fixture_config.json")))
    fixtures = [p for p in products if p.get("attr_tag") == "fixture"]
    by_code = {}
    for key, c in caps.items():
        parts = key.split("_")
        if len(parts) >= 3 and parts[0] == "fixture":
            by_code.setdefault(parts[1], {})[parts[2]] = c

    rows = []
    for p in sorted(fixtures, key=lambda x: (x.get("series_name") or "", x.get("name") or "")):
        code = p["hex"]
        versions = by_code.get(code, {})
        c = versions[max(versions)] if versions else None  # newest hardware version
        fam = family(c)
        st = TESTED.get(code)
        if st:
            status = {"tested": "**Tested**", "production": "**Production**"}[st[0]]
            note = st[1]
        else:
            soon = EXPECTED_SOON.get(p["name"].strip())
            if soon:
                status, note = "Arriving", soon
            elif c is None:
                status, note = "Unknown", "no capability entry in the app table; the importer falls back to on/off + brightness"
            else:
                status, note = "Expected", "same mesh protocol and capability flags as a tested fixture; not yet driven"
        rows.append((p.get("series_name") or "", p["name"], code, ", ".join(sorted(versions)) or "–", fam, mapped(c), not_mapped(c), status, note))

    counts = {}
    for r in rows:
        counts[r[7]] = counts.get(r[7], 0) + 1
    today = datetime.date.today().isoformat()
    lines = [
        "# Fixture support matrix",
        "",
        f"Generated {today} by `tools/fixture-matrix.py` from the amaran Desktop app's own tables (`product.json`, `fixture_config.json`, app bundle v1.1.03). Regenerate after an app update or a new test; edit the `TESTED` table in the script, never this file.",
        "",
        "**How to read it.** amble-home speaks the mesh's generic light commands (on/off, brightness, colour temperature, HSI colour, green/magenta, status). Any fixture that the app table says supports one of those should work the same way the tested ones do: the protocol does not change per model, only the capability flags. Effects, per-pixel control, gels and motion are app features and are not mapped (standing rule 3: the amaran app stays the source of truth for the mesh; HA scenes cover the rest).",
        "",
        "| Status | Meaning |",
        "|---|---|",
        "| **Tested** | driven from Home Assistant through amble-home with status read-back; evidence on the issue named |",
        "| Production | drives the author's studio daily through an amble-home box since 2026-10-27 |",
        "| Arriving | a unit is on its way; tested on arrival |",
        "| Expected | capability flags match a tested fixture; nobody has driven one through amble-home yet |",
        "| Unknown | the app table has no capability entry; the importer falls back to on/off and brightness |",
        "",
        "Counts: " + ", ".join(f"{k.replace('**', '')} {v}" for k, v in sorted(counts.items())) + f" ({len(rows)} fixtures).",
        "",
        "| Series | Model | Code | HW | Family | amble-home maps | Not mapped | Status | Note |",
        "|---|---|---|---|---|---|---|---|---|",
    ]
    for r in rows:
        lines.append("| " + " | ".join(str(x).replace("|", "/") for x in r) + " |")
    lines += [
        "",
        "## Families, for buying one of each",
        "",
        "- **colour (HSI)**: full colour plus CCT; the P60c is this family and is tested.",
        "- **bi-colour (CCT)**: warm-to-cool white only, some with green/magenta; the studio keys are this family.",
        "- **pixel colour**: tubes and bars with many pixels; amble-home drives them as one colour (no per-pixel control). One tested unit would settle whether a whole-tube colour behaves like a panel.",
        "- **single colour**: daylight-only COBs; on/off and brightness only.",
        "",
        "## Facts carried from the studio bridge (`docs/origin.md`)",
        "",
        "- Commands are Telink vendor-model opcodes: on/off, brightness 0–1000, CCT (Kelvin + intensity), HSI (hue, saturation, intensity), status request. Fixtures reply to the app's address `0x0001`; the box overhears.",
        "- HSI and CCT are exclusive modes on the fixture; the last one sent wins, and a fixture remembers its look while off.",
        "- Group `0xC000` and broadcast `0xFFFF` reach every fixture; amble-home never uses them outside an explicit production build, and nothing today needs them.",
        "",
    ]
    with open(a.out, "w") as f:
        f.write("\n".join(lines))
    print(f"wrote {os.path.normpath(a.out)}: {len(rows)} fixtures; " + ", ".join(f"{k.replace('**', '')} {v}" for k, v in sorted(counts.items())))


if __name__ == "__main__":
    sys.exit(main())
