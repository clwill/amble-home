# Fixture support matrix

Generated 2026-11-02 by `tools/fixture-matrix.py` from the amaran Desktop app's own tables (`product.json`, `fixture_config.json`, app bundle v1.1.03). Regenerate after an app update or a new test; edit the `TESTED` table in the script, never this file.

**How to read it.** amble-home speaks the mesh's generic light commands (on/off, brightness, colour temperature, HSI colour, green/magenta, status). Any fixture that the app table says supports one of those should work the same way the tested ones do: the protocol does not change per model, only the capability flags. Effects, per-pixel control, gels and motion are app features and are not mapped (standing rule 3: the amaran app stays the source of truth for the mesh; HA scenes cover the rest).

| Status | Meaning |
|---|---|
| **Tested** | driven from Home Assistant through amble-home with status read-back; evidence on the issue named |
| Production | drives the author's studio daily through an amble-home box since 2026-10-27 |
| Arriving | a unit is on its way; tested on arrival |
| Expected | capability flags match a tested fixture; nobody has driven one through amble-home yet |
| Unknown | the app table has no capability entry; the importer falls back to on/off and brightness |

Counts: Production 5, Tested 2, Expected 73, Unknown 14 (94 fixtures).

| Series | Model | Code | HW | Family | amble-home maps | Not mapped | Status | Note |
|---|---|---|---|---|---|---|---|---|
| Accent | B7c | 04005 | 1.0, 2.0, 3.0 | colour (HSI) | on/off, brightness, CCT 2000–10000 K, HSI colour, green/magenta | effects | Expected | same mesh protocol and capability flags as a tested fixture; not yet driven |
| COB Lights | amaran 100d | 40025 | 1.0 | single colour | on/off, brightness | effects | **Production** | studio keys (two); daily HA use since 2026-10-27 |
| COB Lights | amaran 100d S | 400N5 | 1.0 | single colour | on/off, brightness | effects | Expected | same mesh protocol and capability flags as a tested fixture; not yet driven |
| COB Lights | amaran 100x | 40035 | 1.0 | bi-colour (CCT) | on/off, brightness, CCT 2700–6500 K | effects | Expected | same mesh protocol and capability flags as a tested fixture; not yet driven |
| COB Lights | amaran 100x S | 400O5 | 1.0 | bi-colour (CCT) | on/off, brightness, CCT 2700–6500 K | effects | Expected | same mesh protocol and capability flags as a tested fixture; not yet driven |
| COB Lights | amaran 150c | 400J5 | 1.0 | colour (HSI) | on/off, brightness, CCT 2500–7500 K, HSI colour, green/magenta | effects | **Production** | studio gobo; daily HA use since 2026-10-27 |
| COB Lights | amaran 200c | 400R5 | 1.0 | colour (HSI) | on/off, brightness, CCT 2500–7500 K, HSI colour, green/magenta | effects | Expected | same mesh protocol and capability flags as a tested fixture; not yet driven |
| COB Lights | amaran 200d | 40045 | 1.0 | single colour | on/off, brightness | effects | Expected | same mesh protocol and capability flags as a tested fixture; not yet driven |
| COB Lights | amaran 200d S | 400P5 | 1.0 | single colour | on/off, brightness | effects | Expected | same mesh protocol and capability flags as a tested fixture; not yet driven |
| COB Lights | amaran 200x | 40055 | 1.0 | bi-colour (CCT) | on/off, brightness, CCT 2700–6500 K | effects | Expected | same mesh protocol and capability flags as a tested fixture; not yet driven |
| COB Lights | amaran 200x S | 400Q5 | 1.0 | bi-colour (CCT) | on/off, brightness, CCT 2700–6500 K | effects | Expected | same mesh protocol and capability flags as a tested fixture; not yet driven |
| COB Lights | amaran 300c | 400K5 | 1.0 | colour (HSI) | on/off, brightness, CCT 2500–7500 K, HSI colour, green/magenta | effects | Expected | same mesh protocol and capability flags as a tested fixture; not yet driven |
| COB Lights | amaran 300x | 40195 | 1.0 | bi-colour (CCT) | on/off, brightness, CCT 2700–6500 K | effects | Expected | same mesh protocol and capability flags as a tested fixture; not yet driven |
| COB Lights | amaran 60d S | 400L5 | 1.0 | single colour | on/off, brightness | effects | Expected | same mesh protocol and capability flags as a tested fixture; not yet driven |
| COB Lights | amaran 60x S | 400M5 | 1.0 | bi-colour (CCT) | on/off, brightness, CCT 2700–6500 K | effects | Expected | same mesh protocol and capability flags as a tested fixture; not yet driven |
| COB Lights | amaran COB 60d | 40065 | 1.0 | single colour | on/off, brightness | effects | Expected | same mesh protocol and capability flags as a tested fixture; not yet driven |
| COB Lights | amaran COB 60x | 40075 | 1.0 | bi-colour (CCT) | on/off, brightness, CCT 2700–6500 K | effects | Expected | same mesh protocol and capability flags as a tested fixture; not yet driven |
| COB Lights | amaran Halo 100x | 401C5 | 1.0 | bi-colour (CCT) | on/off, brightness, CCT 2700–6500 K | effects | Expected | same mesh protocol and capability flags as a tested fixture; not yet driven |
| COB Lights | amaran Halo 200x | 401D5 | 1.0 | bi-colour (CCT) | on/off, brightness, CCT 2700–6500 K | effects | Expected | same mesh protocol and capability flags as a tested fixture; not yet driven |
| COB Lights | amaran Halo 300x | 401E5 | 1.0 | bi-colour (CCT) | on/off, brightness, CCT 2700–6500 K | effects | Expected | same mesh protocol and capability flags as a tested fixture; not yet driven |
| COB Lights | amaran Halo 600x | 401F5 | 1.0 | bi-colour (CCT) | on/off, brightness, CCT 2700–6500 K | effects | Expected | same mesh protocol and capability flags as a tested fixture; not yet driven |
| COB Lights | amaran Halo 60x | 401B5 | 1.0 | bi-colour (CCT) | on/off, brightness, CCT 2700–6500 K | effects | Expected | same mesh protocol and capability flags as a tested fixture; not yet driven |
| COB Lights | amaran Ray 120c | 40165 | 1.0 | colour (HSI) | on/off, brightness, CCT 2300–10000 K, HSI colour, green/magenta | effects | Expected | same mesh protocol and capability flags as a tested fixture; not yet driven |
| COB Lights | amaran Ray 360c | 40185 | 1.0 | colour (HSI) | on/off, brightness, CCT 2300–10000 K, HSI colour, green/magenta | effects | Expected | same mesh protocol and capability flags as a tested fixture; not yet driven |
| COB Lights | amaran Ray 60c | 40145 | 1.0 | colour (HSI) | on/off, brightness, CCT 2300–10000 K, HSI colour, green/magenta | effects | Expected | same mesh protocol and capability flags as a tested fixture; not yet driven |
| COB Lights | amaran Ray 660c | 401A5 | 1.0 | colour (HSI) | on/off, brightness, CCT 2300–10000 K, HSI colour, green/magenta | effects | Expected | same mesh protocol and capability flags as a tested fixture; not yet driven |
| Electro Storm | Electro Storm CS15 | 000K5 | 1.0 | colour (HSI) | on/off, brightness, CCT 2000–10000 K, HSI colour, green/magenta | effects, xy colour, gels, motion | Expected | same mesh protocol and capability flags as a tested fixture; not yet driven |
| Electro Storm | Electro Storm XT26 | 000J5 | 1.0 | bi-colour (CCT) | on/off, brightness, CCT 2700–6500 K, green/magenta | effects, motion | Expected | same mesh protocol and capability flags as a tested fixture; not yet driven |
| INFINIBAR | INFINIBAR PB12 | 07008 | 1.0 | pixel colour | on/off, brightness, CCT 2000–10000 K, HSI colour, green/magenta | effects, per-pixel/partition control, xy colour, gels | Expected | same mesh protocol and capability flags as a tested fixture; not yet driven |
| INFINIBAR | INFINIBAR PB3 | 07006 | 1.0 | pixel colour | on/off, brightness, CCT 2000–10000 K, HSI colour, green/magenta | effects, per-pixel/partition control, xy colour, gels | Expected | same mesh protocol and capability flags as a tested fixture; not yet driven |
| INFINIBAR | INFINIBAR PB6 | 07007 | 1.0 | pixel colour | on/off, brightness, CCT 2000–10000 K, HSI colour, green/magenta | effects, per-pixel/partition control, xy colour, gels | Expected | same mesh protocol and capability flags as a tested fixture; not yet driven |
| INFINIMAT | INFINIMAT 16 | 09015 | 1.0 | colour (HSI) | on/off, brightness, CCT 2000–10000 K, HSI colour, green/magenta | effects, xy colour, gels | Expected | same mesh protocol and capability flags as a tested fixture; not yet driven |
| INFINIMAT | INFINIMAT 4 | 09005 | 1.0 | colour (HSI) | on/off, brightness, CCT 2000–10000 K, HSI colour, green/magenta | effects, xy colour, gels | Expected | same mesh protocol and capability flags as a tested fixture; not yet driven |
| LED Mats | amaran F21c | 400C5 | 1.0 | colour (HSI) | on/off, brightness, CCT 2500–7500 K, HSI colour, green/magenta | effects | Expected | same mesh protocol and capability flags as a tested fixture; not yet driven |
| LED Mats | amaran F21x | 400B5 | 1.0 | bi-colour (CCT) | on/off, brightness, CCT 2500–7500 K | effects | Expected | same mesh protocol and capability flags as a tested fixture; not yet driven |
| LED Mats | amaran F22c | 400E5 | 1.0 | colour (HSI) | on/off, brightness, CCT 2500–7500 K, HSI colour, green/magenta | effects | Expected | same mesh protocol and capability flags as a tested fixture; not yet driven |
| LED Mats | amaran F22x | 400D5 | 1.0 | bi-colour (CCT) | on/off, brightness, CCT 2500–7500 K | effects | Expected | same mesh protocol and capability flags as a tested fixture; not yet driven |
| LED Mats | amaran F7c | 400A5 | – | unknown | on/off, brightness (capabilities unknown to the app table) |  | Unknown | no capability entry in the app table; the importer falls back to on/off + brightness |
| Light Storm | LS 1200d Pro | 000E5 | 1.0 | single colour | on/off, brightness | effects | Expected | same mesh protocol and capability flags as a tested fixture; not yet driven |
| Light Storm | LS 1500d | 00065 | – | unknown | on/off, brightness (capabilities unknown to the app table) |  | Unknown | no capability entry in the app table; the importer falls back to on/off + brightness |
| Light Storm | LS 1500x Pro | 000I5 | – | unknown | on/off, brightness (capabilities unknown to the app table) |  | Unknown | no capability entry in the app table; the importer falls back to on/off + brightness |
| Light Storm | LS 150x | 00035 | – | unknown | on/off, brightness (capabilities unknown to the app table) |  | Unknown | no capability entry in the app table; the importer falls back to on/off + brightness |
| Light Storm | LS 300x | 00045 | 1.0, 2.0 | bi-colour (CCT) | on/off, brightness, CCT 2700–6500 K | effects | Expected | same mesh protocol and capability flags as a tested fixture; not yet driven |
| Light Storm | LS 350d | 000B5 | – | unknown | on/off, brightness (capabilities unknown to the app table) |  | Unknown | no capability entry in the app table; the importer falls back to on/off + brightness |
| Light Storm | LS 50d | 00015 | – | unknown | on/off, brightness (capabilities unknown to the app table) |  | Unknown | no capability entry in the app table; the importer falls back to on/off + brightness |
| Light Storm | LS 50x | 00025 | – | unknown | on/off, brightness (capabilities unknown to the app table) |  | Unknown | no capability entry in the app table; the importer falls back to on/off + brightness |
| Light Storm | LS 600X Pro | 000D5 | 1.0 | bi-colour (CCT) | on/off, brightness, CCT 2700–6500 K | effects | Expected | same mesh protocol and capability flags as a tested fixture; not yet driven |
| Light Storm | LS 600c Pro | 000G5 | 1.0 | colour (HSI) | on/off, brightness, CCT 2300–10000 K, HSI colour, green/magenta | effects, xy colour, gels | Expected | same mesh protocol and capability flags as a tested fixture; not yet driven |
| Light Storm | LS 600c Pro II | 000L5 | 1.0 | colour (HSI) | on/off, brightness, CCT 2300–10000 K, HSI colour, green/magenta | effects, xy colour, gels | Expected | same mesh protocol and capability flags as a tested fixture; not yet driven |
| Light Storm | LS 600d | 000C5 | 1.0 | single colour | on/off, brightness | effects | Expected | same mesh protocol and capability flags as a tested fixture; not yet driven |
| Light Storm | LS 600d Pro | 00055 | 1.0 | single colour | on/off, brightness | effects | Expected | same mesh protocol and capability flags as a tested fixture; not yet driven |
| Light Storm | LS 600x | 00095 | – | unknown | on/off, brightness (capabilities unknown to the app table) |  | Unknown | no capability entry in the app table; the importer falls back to on/off + brightness |
| Light Storm | LS 60d | 00075 | 1.0, 1.1 | single colour | on/off, brightness | effects | Expected | same mesh protocol and capability flags as a tested fixture; not yet driven |
| Light Storm | LS 60x | 00085 | 1.0, 1.1, 2.0 | bi-colour (CCT) | on/off, brightness, CCT 2700–6500 K | effects | Expected | same mesh protocol and capability flags as a tested fixture; not yet driven |
| Light Storm | LS C300d II | 00005 | 1.0 | single colour | on/off, brightness | effects | Expected | same mesh protocol and capability flags as a tested fixture; not yet driven |
| Mini | MC | 05006 | 1.2 | colour (HSI) | on/off, brightness, CCT 3200–6500 K, HSI colour | effects | Expected | same mesh protocol and capability flags as a tested fixture; not yet driven |
| Mini | MC | 05008 | 1.0 | colour (HSI) | on/off, brightness, CCT 3200–6500 K, HSI colour | effects | Expected | same mesh protocol and capability flags as a tested fixture; not yet driven |
| Mini | MC | 05005 | 1.3, 2.0 | colour (HSI) | on/off, brightness, CCT 3200–6500 K, HSI colour | effects | Expected | same mesh protocol and capability flags as a tested fixture; not yet driven |
| Mini | MC | 05007 | 1.1 | colour (HSI) | on/off, brightness, CCT 3200–6500 K, HSI colour | effects | Expected | same mesh protocol and capability flags as a tested fixture; not yet driven |
| Mini | MC Pro | 05010 | 1.0 | colour (HSI) | on/off, brightness, CCT 2000–10000 K, HSI colour, green/magenta | effects, xy colour, gels | Expected | same mesh protocol and capability flags as a tested fixture; not yet driven |
| Mini | MT Pro | 000F5 | 1.0, 2.0 | pixel colour | on/off, brightness, CCT 2000–10000 K, HSI colour, green/magenta | effects, per-pixel/partition control, xy colour, gels | Expected | same mesh protocol and capability flags as a tested fixture; not yet driven |
| Mini Lights | amaran Ace 25c | 400U5 | 1.0 | colour (HSI) | on/off, brightness, CCT 2300–10000 K, HSI colour, green/magenta | effects | **Production** | studio hair; daily HA use since 2026-10-27 |
| Mini Lights | amaran Ace 25x | 400T5 | 1.0 | bi-colour (CCT) | on/off, brightness, CCT 2700–6500 K | effects | Expected | same mesh protocol and capability flags as a tested fixture; not yet driven |
| Mini Lights | amaran Go | 400V5 | 1.0 | bi-colour (CCT) | on/off, brightness, CCT 2700–6500 K | effects | Expected | same mesh protocol and capability flags as a tested fixture; not yet driven |
| Motorized | Motorized F14 Fresnel | EM015 | – | unknown | on/off, brightness (capabilities unknown to the app table) |  | Unknown | no capability entry in the app table; the importer falls back to on/off + brightness |
| Motorized | Motorized Yoke | EM005 | – | unknown | on/off, brightness (capabilities unknown to the app table) |  | Unknown | no capability entry in the app table; the importer falls back to on/off + brightness |
| Nova | NOVA 9° 2×1 | 02075 | 1.0 | pixel colour | on/off, brightness, CCT 1800–20000 K, HSI colour, green/magenta | effects, per-pixel/partition control, xy colour | Expected | same mesh protocol and capability flags as a tested fixture; not yet driven |
| Nova | NOVA II 1x1 | 02085 | 1.0 | colour (HSI) | on/off, brightness, CCT 1800–20000 K, HSI colour, green/magenta | effects, xy colour, gels | Expected | same mesh protocol and capability flags as a tested fixture; not yet driven |
| Nova | NOVA II 2x1 | 02065 | 1.0 | colour (HSI) | on/off, brightness, CCT 1800–20000 K, HSI colour, green/magenta | effects, xy colour, gels | Expected | same mesh protocol and capability flags as a tested fixture; not yet driven |
| Nova | Nova P300c | 02005 | 1.0, 1.1, 1.2, 1.3, 1.4, 2.0 | colour (HSI) | on/off, brightness, CCT 2000–10000 K, HSI colour, green/magenta | effects, xy colour, gels | Expected | same mesh protocol and capability flags as a tested fixture; not yet driven |
| Nova | Nova P600C | 02035 | 1.0, 2.0, 3.0, 4.0 | colour (HSI) | on/off, brightness, CCT 2000–10000 K, HSI colour, green/magenta | effects, xy colour, gels | Expected | same mesh protocol and capability flags as a tested fixture; not yet driven |
| Other | amaran SM5c | 400F5 | 1.0, 1.1 | colour (HSI) | on/off, brightness, CCT 3200–6500 K, HSI colour, green/magenta | effects, per-pixel/partition control | Expected | same mesh protocol and capability flags as a tested fixture; not yet driven |
| Panels | amaran P40x | 400S5 | 1.0 | bi-colour (CCT) | on/off, brightness, CCT 2700–6500 K | effects | Expected | same mesh protocol and capability flags as a tested fixture; not yet driven |
| Panels | amaran P60c | 40015 | 1.0 | colour (HSI) | on/off, brightness, CCT 2500–7500 K, HSI colour, green/magenta | effects | **Tested** | P60c: on/off, brightness, CCT, HSI, green/magenta, status read-back; milestone 2–4 acceptance, 2026-10-26 |
| Panels | amaran P60x | 40005 | 1.0 | bi-colour (CCT) | on/off, brightness, CCT 3200–6500 K | effects | Expected | same mesh protocol and capability flags as a tested fixture; not yet driven |
| Panels | amaran Pano 120c | 400X5 | 1.0 | colour (HSI) | on/off, brightness, CCT 2300–10000 K, HSI colour, green/magenta | effects | Expected | same mesh protocol and capability flags as a tested fixture; not yet driven |
| Panels | amaran Pano 60c | 400W5 | 1.0 | colour (HSI) | on/off, brightness, CCT 2300–10000 K, HSI colour, green/magenta | effects | Expected | same mesh protocol and capability flags as a tested fixture; not yet driven |
| Panels | amaran Verge | 400Y5 | 1.0 | bi-colour (CCT) | on/off, brightness, CCT 2700–6500 K | effects | Expected | same mesh protocol and capability flags as a tested fixture; not yet driven |
| Panels | amaran Verge Max | 400Z5 | 1.0 | bi-colour (CCT) | on/off, brightness, CCT 2700–6500 K | effects | **Tested** | Verge Max: imported through the account door, then from HA on/off, brightness 1–100 %, CCT 2700/5000/6500 K, each read back; 2026-10-29 |
| SGC-Tube | SGC-H120 | 20015 | – | unknown | on/off, brightness (capabilities unknown to the app table) |  | Unknown | no capability entry in the app table; the importer falls back to on/off + brightness |
| SGC-Tube | SGC-H60 | 20005 | – | unknown | on/off, brightness (capabilities unknown to the app table) |  | Unknown | no capability entry in the app table; the importer falls back to on/off + brightness |
| SGC-Tube | SGC-P120 | 20035 | – | unknown | on/off, brightness (capabilities unknown to the app table) |  | Unknown | no capability entry in the app table; the importer falls back to on/off + brightness |
| SGC-Tube | SGC-P60 | 20025 | – | unknown | on/off, brightness (capabilities unknown to the app table) |  | Unknown | no capability entry in the app table; the importer falls back to on/off + brightness |
| STORM | STORM 1000c | 000P5 | 1.0 | colour (HSI) | on/off, brightness, CCT 1800–20000 K, HSI colour, green/magenta | effects, xy colour, gels | Expected | same mesh protocol and capability flags as a tested fixture; not yet driven |
| STORM | STORM 1200x | 000M5 | 1.0 | bi-colour (CCT) | on/off, brightness, CCT 2500–10000 K, green/magenta | effects, xy colour | Expected | same mesh protocol and capability flags as a tested fixture; not yet driven |
| STORM | STORM 400x | 000Q5 | 1.0 | bi-colour (CCT) | on/off, brightness, CCT 2500–10000 K, green/magenta | effects, xy colour | Expected | same mesh protocol and capability flags as a tested fixture; not yet driven |
| STORM | STORM 700x | 000S5 | 1.0 | colour (HSI) | on/off, brightness, CCT 2500–10000 K, HSI colour, green/magenta | effects, xy colour | Expected | same mesh protocol and capability flags as a tested fixture; not yet driven |
| STORM | STORM 80c | 000N5 | 1.0 | colour (HSI) | on/off, brightness, CCT 1800–20000 K, HSI colour, green/magenta | effects, xy colour, gels | Expected | same mesh protocol and capability flags as a tested fixture; not yet driven |
| STORM | STORM XT52 | 000R5 | 1.0 | colour (HSI) | on/off, brightness, CCT 2500–10000 K, HSI colour, green/magenta | effects, xy colour | Expected | same mesh protocol and capability flags as a tested fixture; not yet driven |
| Tubes | amaran PT1c | 400G5 | 1.0 | pixel colour | on/off, brightness, CCT 2700–10000 K, HSI colour, green/magenta | effects, per-pixel/partition control | Expected | same mesh protocol and capability flags as a tested fixture; not yet driven |
| Tubes | amaran PT2c | 400H5 | 1.0 | pixel colour | on/off, brightness, CCT 2700–10000 K, HSI colour, green/magenta | effects, per-pixel/partition control | Expected | same mesh protocol and capability flags as a tested fixture; not yet driven |
| Tubes | amaran PT4c | 400I5 | 1.0 | pixel colour | on/off, brightness, CCT 2700–10000 K, HSI colour, green/magenta | effects, per-pixel/partition control | Expected | same mesh protocol and capability flags as a tested fixture; not yet driven |
| Tubes | amaran T2c  | 40085 | 1.0 | colour (HSI) | on/off, brightness, CCT 2500–7500 K, HSI colour, green/magenta | effects | **Production** | studio accent; daily HA use since 2026-10-27 |
| Tubes | amaran T4c  | 40095 | 1.0 | colour (HSI) | on/off, brightness, CCT 2500–7500 K, HSI colour, green/magenta | effects | **Production** | studio tubes (two); daily HA use since 2026-10-27 |

## Families, for buying one of each

- **colour (HSI)**: full colour plus CCT; the P60c is this family and is tested.
- **bi-colour (CCT)**: warm-to-cool white only, some with green/magenta; the studio keys are this family.
- **pixel colour**: tubes and bars with many pixels; amble-home drives them as one colour (no per-pixel control). One tested unit would settle whether a whole-tube colour behaves like a panel.
- **single colour**: daylight-only COBs; on/off and brightness only.

## Facts carried from the studio bridge (`docs/origin.md`)

- Commands are Telink vendor-model opcodes: on/off, brightness 0–1000, CCT (Kelvin + intensity), HSI (hue, saturation, intensity), status request. Fixtures reply to the app's address `0x0001`; the box overhears.
- HSI and CCT are exclusive modes on the fixture; the last one sent wins, and a fixture remembers its look while off.
- Group `0xC000` and broadcast `0xFFFF` reach every fixture; amble-home never uses them outside an explicit production build, and nothing today needs them.
