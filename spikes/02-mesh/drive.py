#!/usr/bin/env python3
"""Spike 2 bench driver: talks to the dev board's ESPHome web server REST API.
Prints only counters, sequence numbers and the P60c's decoded state (no keys).

  python3 drive.py <host> status            # counters + last P60c status
  python3 drive.py <host> on|off            # P60c power
  python3 drive.py <host> bright <0-100>
  python3 drive.py <host> cct <2500-7500>
  python3 drive.py <host> hue <0-360>
  python3 drive.py <host> poll              # ask the P60c for its status
  python3 drive.py <host> reboot
  python3 drive.py <host> watch <seconds>   # print status every 2 s
"""
import json, sys, time, urllib.parse, urllib.request

host = sys.argv[1]
cmd = sys.argv[2] if len(sys.argv) > 2 else "status"
arg = sys.argv[3] if len(sys.argv) > 3 else None
BASE = f"http://{host}"

q = urllib.parse.quote

def get(path):
    with urllib.request.urlopen(BASE + path, timeout=5) as r:
        return json.loads(r.read().decode())

def post(path):
    req = urllib.request.Request(BASE + path, method="POST", data=b"")
    with urllib.request.urlopen(req, timeout=5) as r:
        return r.status

def status():
    # ESPHome 2026's web server keys REST endpoints by the entity *name*, URL-encoded.
    ids = {"total": "Snooped status replies", "p60c": "Snooped P60c replies", "sent": "Commands sent",
           "seq": "Mesh sequence number", "iv": "Mesh IV index"}
    s = {k: int(get(f"/sensor/{q(v)}")["value"]) for k, v in ids.items()}
    last = get(f"/text_sensor/{q('Last P60c status')}")["value"]
    print(f"{time.strftime('%H:%M:%S')} snooped total={s['total']} p60c={s['p60c']} sent={s['sent']} seq={s['seq']} iv={s['iv']} | last: {last}")

if cmd == "status":
    status()
elif cmd in ("on", "off"):
    post(f"/switch/{q('P60c power')}/turn_{cmd}"); print(f"sent power {cmd}")
elif cmd == "bright":
    post(f"/number/{q('P60c brightness')}/set?value={arg}"); print(f"sent brightness {arg}%")
elif cmd == "cct":
    post(f"/number/{q('P60c CCT')}/set?value={arg}"); print(f"sent cct {arg}K")
elif cmd == "hue":
    post(f"/number/{q('P60c hue')}/set?value={arg}"); print(f"sent hue {arg}")
elif cmd == "poll":
    post(f"/button/{q('P60c status request')}/press"); print("sent status request")
elif cmd == "reboot":
    post(f"/button/{q('Reboot')}/press"); print("reboot requested")
elif cmd == "watch":
    end = time.time() + float(arg or 30)
    while time.time() < end:
        try: status()
        except Exception as e: print("…", e.__class__.__name__)
        time.sleep(2)
else:
    print(__doc__); sys.exit(2)
