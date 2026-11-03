"""Spike 4 reverse-mode capture: mitmdump listens on 443 (the hosts file points the cloud names at
this Mac) and forwards each request to the real server by IP (the hosts file would loop otherwise),
keeping the client's Host header and sending the real name as SNI. Recording is capture_addon's:
shapes only, no secrets."""
import os, sys
sys.path.insert(0, os.path.dirname(os.path.abspath(__file__)))
import capture_addon
from mitmproxy import ctx, http

REAL = {"us.sidus.link": "54.241.36.224", "gs.sidus.link": "184.169.187.240"}

def load(loader):
    ctx.options.keep_host_header = True
    ctx.options.connection_strategy = "lazy"

def request(flow: http.HTTPFlow):
    host = (flow.request.host_header or "").split(":")[0]
    if host in REAL:
        flow.request.host = REAL[host]
        flow.request.port = 443
        flow.request.scheme = "https"
        flow.request.host_header = host

def server_connect(data):
    ip = data.server.address[0] if data.server.address else None
    for name, real in REAL.items():
        if real == ip:
            data.server.sni = name

def response(flow: http.HTTPFlow):
    try:
        capture_addon.response(flow)
        ctx.log.info(f"recorded {flow.request.host_header} {flow.request.path.split('?')[0]}")
    except Exception as e:
        ctx.log.error(f"capture failed: {e!r}")
