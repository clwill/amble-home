#!/usr/bin/env python3
"""Spike 4 (#8): one login and one full sync query against the amaran cloud, printing SHAPES only.
Run: python3 spikes/04-cloud/cloud-replay.py (asks for the amaran account password; nothing is stored; shapes go to shapes.jsonl beside it)."""
import getpass, json, ssl, sys, urllib.request, uuid
import re
SECRET = re.compile(r"(key|token|secret|password|passwd|pwd|sign|auth|mac|cookie|session|credential|iv|uuid|email|phone|account|user)", re.I)
def shape(obj):   # same redaction as the proxy: strings become <str len=N>, secret-looking keys are removed
    if isinstance(obj, dict): return {k: ("<redacted>" if SECRET.search(k) else shape(v)) for k, v in obj.items()}
    if isinstance(obj, list): return ([shape(obj[0])] + ([f"<+{len(obj)-1} more>"] if len(obj) > 1 else [])) if obj else []
    if isinstance(obj, str): return f"<str len={len(obj)}>" if obj else "<str empty>"
    return obj

import os
S = os.path.dirname(os.path.abspath(__file__))
ctx = ssl.create_default_context()   # during the spike this pointed at a bundle that also trusted the recording proxy
HEAD = {"Content-Type": "application/json", "platform": "7", "request_source": "4", "product_version": "1.1.03",
        "device_ui_type": "0", "device_language": "en", "device_name": "amble-home spike", "phone_model": "amble-home spike",
        "device_identifier": str(uuid.uuid4()), "User-Agent": "amaran-desktop/1.1.03 spike4"}
out = open(S + "/replay.jsonl", "a")

def call(method, url, body=None, headers=None):
    data = json.dumps(body).encode() if body is not None else None
    req = urllib.request.Request(url, data=data, method=method, headers={**HEAD, **(headers or {})})
    try:
        with urllib.request.urlopen(req, context=ctx, timeout=30) as r: raw = r.read(); status = r.status
    except urllib.error.HTTPError as e: raw = e.read(); status = e.code
    try: parsed = json.loads(raw)
    except Exception: parsed = {"_unparseable_bytes": len(raw)}
    rec = {"method": method, "url": url.split("?")[0], "status": status, "code": parsed.get("code") if isinstance(parsed, dict) else None, "resp": shape(parsed)}
    out.write(json.dumps(rec) + "\n"); out.flush()
    print(method, url.split("?")[0], "->", status, "code", rec["code"])
    return parsed

email = input("amaran account email: ").strip()
pw = getpass.getpass("amaran password (not echoed, not stored): ")
login = call("POST", "https://us.sidus.link/uc/user/login", {"email": email, "password": pw, "area": "US"})
del pw
token = (login.get("data") or {}).get("token") if isinstance(login, dict) else None
if not token:
    print("login did not return a token; msg:", (login.get("msg") if isinstance(login, dict) else "?")); sys.exit(1)
auth = {"token": token}
zeros = "&".join(f"{k}_update_time=0" for k in ("mesh","fixture","scene","group","quickshot","preset","scene_detail","group_detail","quickshot_detail","camera","camera_preset"))
pack = call("GET", "https://us.sidus.link/amaran/pack/query?" + zeros + "&attr_tags=fixture,camera,microphone", headers=auth)
d = (pack.get("data") or {}) if isinstance(pack, dict) else {}
for k in ("meshs", "fixtures", "scenes", "groups"):
    v = d.get(k) or {}
    print(f"  {k}: {len(v.get('existing') or [])} existing, {len(v.get('deleted') or [])} deleted")
call("GET", "https://us.sidus.link/amaran/mesh/importableV2", headers=auth)
print("done; shapes written to", S + "/replay.jsonl")
