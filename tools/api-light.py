#!/usr/bin/env python3
"""Bench stand-in for Home Assistant: drive a box's lights over the ESPHome native
API (the same protocol HA uses). Prints entity types, keys and states only — names
come from the user's amaran database and are never printed (standing rule 2).
Address a light as #<n> (its position in `list`) or by its exact name.

  api-light.py <host> list
  api-light.py <host> #0|<light-name> on|off
  api-light.py <host> <light-name> brightness <0-100>
  api-light.py <host> <light-name> ct <kelvin>
  api-light.py <host> <light-name> rgb <r> <g> <b>       (0-255 each)
  api-light.py <host> <light-name> watch <seconds>
"""
import asyncio, sys
from aioesphomeapi import APIClient, LightInfo, LightState, ColorMode

async def main():
    host, cmd = sys.argv[1], sys.argv[2]
    cli = APIClient(host, 6053, None)
    await cli.connect(login=True)
    info, _ = await cli.list_entities_services()
    lights = [e for e in info if isinstance(e, LightInfo)]
    if cmd == "diag":
        # Diagnostics as HA sees them, then how often each one actually publishes.
        from aioesphomeapi import SensorInfo, SensorState, BinarySensorInfo
        secs = int(sys.argv[3]) if len(sys.argv) > 3 else 60
        diags = {e.key: e for e in info if isinstance(e, (SensorInfo, BinarySensorInfo))}
        for e in diags.values():
            unit = getattr(e, "unit_of_measurement", "") or "(no unit)"
            sc = getattr(e, "state_class", None); sc = sc.name if sc is not None else "-"
            print(f"  {e.name}: unit {unit}, state_class {sc}, category {e.entity_category.name}")
        counts = {}
        def on_any(st):
            if st.key in diags: counts[diags[st.key].name] = counts.get(diags[st.key].name, 0) + 1
        cli.subscribe_states(on_any)
        await asyncio.sleep(secs)
        print(f"  state updates in {secs}s (the first of each is the initial state):")
        for name in sorted(diags[k].name for k in diags): print(f"    {name}: {counts.get(name, 0)}")
        await cli.disconnect(); return
    if cmd == "list":
        for i, _ in enumerate(lights):
            print(f"  light #{i}")
        kinds = {}
        for e in info:
            if not isinstance(e, LightInfo): kinds[type(e).__name__] = kinds.get(type(e).__name__, 0) + 1
        print("  other entities:", ", ".join(f"{k}×{v}" for k, v in sorted(kinds.items())) or "none")
        await cli.disconnect(); return
    name, action, args = cmd, sys.argv[3], sys.argv[4:]
    if name.startswith("#") and name[1:].isdigit() and int(name[1:]) < len(lights):
        light = lights[int(name[1:])]
    else:
        light = next((l for l in lights if l.name == name), None)
    if light is None:
        print(f"no such light; {len(lights)} light(s) available, address them as #0..#{len(lights)-1}"); await cli.disconnect(); return 2
    states = {}
    def on_state(s):
        if isinstance(s, LightState) and s.key == light.key:
            states["last"] = s
            print(f"    state: on={s.state} brightness={s.brightness:.2f} mode={ColorMode(s.color_mode).name if s.color_mode else '?'} ct={s.color_temperature:.0f}mired rgb=({s.red:.2f},{s.green:.2f},{s.blue:.2f})")  # no keys, no names
    cli.subscribe_states(on_state)
    await asyncio.sleep(1.0)
    k = light.key
    if action == "on": cli.light_command(key=k, state=True)
    elif action == "off": cli.light_command(key=k, state=False)
    elif action == "brightness": cli.light_command(key=k, state=True, brightness=float(args[0]) / 100.0)
    elif action == "ct": cli.light_command(key=k, state=True, color_mode=ColorMode.COLOR_TEMPERATURE, color_temperature=1_000_000.0 / float(args[0]))
    elif action == "rgb":
        r, g, b = (int(x) / 255.0 for x in args[:3])
        cli.light_command(key=k, state=True, color_mode=ColorMode.RGB, rgb=(r, g, b), color_brightness=1.0)
    elif action == "watch":
        await asyncio.sleep(float(args[0] if args else 30)); await cli.disconnect(); return
    else:
        print(__doc__); await cli.disconnect(); return 2
    print(f"  sent {action} {' '.join(args)} to light #{lights.index(light)}")
    await asyncio.sleep(4.0)   # let the fixture's status reply come back through the snoop
    await cli.disconnect()

sys.exit(asyncio.run(main()) or 0)
