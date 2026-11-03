"""mitmdump addon for Spike 4: record the SHAPE of the amaran cloud traffic, never its secrets.
Writes one JSON line per flow to CAPTURE_OUT. Every string value is replaced by a type+length marker;
header values are kept only for a short allowlist (content-type, content-length, status). Numbers and
booleans pass through, except keys that look secret, which are redacted whatever their type.
"""
import json, os, re, time
from mitmproxy import http

OUT = os.environ.get("CAPTURE_OUT", "capture.jsonl")
KEEP_HEADERS = {"content-type", "content-length", "accept", "user-agent", "accept-encoding", "connection", "host"}
SECRET = re.compile(r"(key|token|secret|password|passwd|pwd|sign|auth|mac|cookie|session|credential|iv|uuid|email|phone|account|user)", re.I)
HOSTS = ("sidus.link",)

def mark(v):
    if isinstance(v, str): return f"<str len={len(v)}>" if v else "<str empty>"
    return v

def shape(obj, path=""):
    if isinstance(obj, dict):
        return {k: ("<redacted>" if SECRET.search(k) else shape(v, path + "/" + k)) for k, v in obj.items()}
    if isinstance(obj, list):
        return [shape(obj[0], path + "[]")] + ([f"<+{len(obj)-1} more>"] if len(obj) > 1 else []) if obj else []
    return mark(obj)

def body_shape(msg):
    ct = (msg.headers.get("content-type") or "").lower()
    raw = msg.get_content() or b""
    if not raw: return None
    if "json" in ct:
        try: return shape(json.loads(raw))
        except Exception: return f"<unparseable json {len(raw)}B>"
    if "x-www-form-urlencoded" in ct:
        from urllib.parse import parse_qsl
        return shape({k: v for k, v in parse_qsl(raw.decode("utf-8", "replace"))})
    return f"<{ct or 'unknown'} {len(raw)}B>"

def headers(h):
    return {k: (v if k.lower() in KEEP_HEADERS else f"<{len(v)} chars>") for k, v in h.items()}

def response(flow: http.HTTPFlow):
    host = (flow.request.host_header or flow.request.pretty_host or "")
    if not any(h in host for h in HOSTS): return
    q = {k: "<redacted>" if SECRET.search(k) else mark(v) for k, v in flow.request.query.items()}
    rec = {"t": round(time.time(), 1), "method": flow.request.method, "host": host,
           "path": flow.request.path.split("?")[0], "query": q, "req_headers": headers(flow.request.headers),
           "req_body": body_shape(flow.request), "status": flow.response.status_code,
           "resp_headers": headers(flow.response.headers), "resp_body": body_shape(flow.response)}
    with open(OUT, "a") as f: f.write(json.dumps(rec) + "\n")
