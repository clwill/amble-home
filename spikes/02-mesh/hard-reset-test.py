#!/usr/bin/env python3
"""Spike 2: hard-reset (EN pulse over USB-JTAG, the bench stand-in for power loss),
then check the mesh sequence number did not go backwards and the P60c still obeys.
Captures the boot log over USB with the docs/bench.md recipe. Prints no keys."""
import glob, os, re, subprocess, sys, time, urllib.request
import serial

H = os.environ.get("AMBLE_HOST", "amble-home.local")  # the bench box
PORT = os.environ.get("AMBLE_PORT", "/dev/cu.usbmodem101")  # its native USB port
ansi = re.compile(r"\x1b\[[0-9;]*m")

def d(*a):
    r = subprocess.run([sys.executable, "drive.py", H, *a], capture_output=True, text=True, timeout=20)
    out = (r.stdout + r.stderr).strip().splitlines(); print("   ", out[-1] if out else "(no output)")

def read_for(p, sec):
    t0 = time.time(); buf = b""
    while time.time() - t0 < sec: buf += p.read(4096)
    return buf.decode("utf-8", "replace")

print("## before hard reset"); d("status")
p = serial.Serial(PORT, 115200, timeout=0.2); read_for(p, 0.3)
p.dtr, p.rts = False, True; time.sleep(0.3); p.dtr, p.rts = True, True   # EN pulse, IO0 high
banner = read_for(p, 0.5); p.close()
print("## ROM:", " | ".join(l for l in banner.splitlines() if "rst:" in l))
t0 = time.time(); p = None
while time.time() - t0 < 8:
    try:
        ports = glob.glob("/dev/cu.usbmodem*")
        if ports: p = serial.Serial(ports[0], 115200, timeout=0.2); break
    except Exception: pass
    time.sleep(0.05)
boot = ansi.sub("", read_for(p, 25)) if p else ""
if p: p.close()
keep = [l for l in boot.splitlines() if re.search(r"amble_spike2|CRASH|restored|provision|app key|safe_mode", l)]
print("## boot log (filtered):"); print("\n".join("    " + l for l in keep[:20]))
for _ in range(60):
    try: urllib.request.urlopen(f"http://{H}/", timeout=2); break
    except Exception: time.sleep(1)
time.sleep(3); print("## after hard reset"); d("status")
print("## ON + poll"); d("on"); time.sleep(1.2); d("poll"); time.sleep(2.5); d("status")
print("## OFF + poll"); d("off"); time.sleep(1.2); d("poll"); time.sleep(2.5); d("status")
