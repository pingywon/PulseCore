#!/usr/bin/env python3
"""
PulseFeed Bridge -- a safety-gated reverse proxy for the controller.

WHY THIS IS NOT JUST `nginx proxy_pass`
---------------------------------------
The thing on the other side of this proxy opens valves and spins a
motor. Putting that on the public internet with a plain pass-through
means a credential leak, a replayed URL, or a bored scanner is one
request away from starting a machine nobody is standing next to.

So the bridge is deliberately more than a forwarder:

  * It has its own auth, separate from the device PIN, so you can hand
    someone a share link without handing them the device.
  * It runs in an explicit MODE that caps what can be done remotely.
    The default mode cannot start the machine. You have to opt into
    that, on the command line, every time.
  * Safety actions (stop, e-stop) are permitted in every mode including
    the read-only one. If remote access can see a problem it must
    always be able to stop it.
  * Every state-changing request is written to an append-only audit
    log with the source address.
  * Per-IP rate limiting, because the device is a single-threaded web
    server and a trivial flood would otherwise wedge it.

MODES (--mode), least to most permissive
----------------------------------------
  readonly    telemetry only. No mutations except stop/e-stop.
  supervise   readonly + settings you can turn DOWN, + stop/e-stop.
              (default)
  control     everything except starting the machine remotely.
  full        everything, including remote start. Say --i-understand.

USAGE
-----
  # simplest: expose a device to the LAN with a share token
  ./pulsefeed-bridge.py --target http://192.168.13.50

  # read-only public link over an existing tunnel
  ./pulsefeed-bridge.py --target http://192.168.13.50 \
      --listen 0.0.0.0:8443 --mode readonly

  # with TLS
  ./pulsefeed-bridge.py --target http://192.168.13.50 \
      --cert cert.pem --key key.pem

For actual internet exposure without port-forwarding, put a Cloudflare
Tunnel in front of THIS process, not in front of the device. See
bridge/tunnel/README.md.
"""

import argparse
import base64
import hashlib
import hmac
import ipaddress
import json
import os
import secrets
import ssl
import sys
import threading
import time
import urllib.error
import urllib.parse
import urllib.request
from collections import defaultdict, deque
from http.server import BaseHTTPRequestHandler, ThreadingHTTPServer

VERSION = "2.0.0"

# --------------------------------------------------------------------- #
#  Policy
# --------------------------------------------------------------------- #

MODES = ("readonly", "supervise", "control", "full")

# Actions on /api/v1/control that each mode permits.
CONTROL_ACTIONS = {
    "readonly":  {"stop", "estop", "release"},
    "supervise": {"stop", "estop", "release", "clear"},
    "control":   {"stop", "estop", "release", "clear"},
    "full":      {"stop", "estop", "release", "clear", "start"},
}

# Settings keys each mode may write.
SETTINGS_KEYS = {
    "readonly":  set(),
    # supervise may only reduce output; enforced numerically below.
    "supervise": {"vac", "ppm", "motor", "ratio", "ramp", "runLimitMin"},
    "control":   {"vac", "ppm", "motor", "ratio", "ramp", "runLimitMin",
                  "motorSoftMs", "backlight", "dark", "autoStop"},
    "full":      None,   # None means "no restriction"
}

# Paths that never require bridge auth.
OPEN_PATHS = {"/healthz", "/bridge/login", "/bridge/style.css"}

# Endpoints the bridge refuses to forward in any mode. These change the
# device's own security posture or its network identity, and doing that
# from the far side of the internet is never what you want.
BLOCKED_ALWAYS = {
    "/api/v1/wifi/join",
    "/api/v1/wifi/forget",
}
BLOCKED_UNLESS_FULL = {
    "/api/v1/system",
}


# --------------------------------------------------------------------- #
#  Rate limiting
# --------------------------------------------------------------------- #

class RateLimiter:
    """Sliding-window counter, per client address."""

    def __init__(self, limit_per_min: int):
        self.limit = limit_per_min
        self.hits = defaultdict(deque)
        self.lock = threading.Lock()

    def allow(self, key: str) -> bool:
        if self.limit <= 0:
            return True
        now = time.time()
        with self.lock:
            q = self.hits[key]
            while q and now - q[0] > 60.0:
                q.popleft()
            if len(q) >= self.limit:
                return False
            q.append(now)
            return True


# --------------------------------------------------------------------- #
#  Sessions
# --------------------------------------------------------------------- #

class Sessions:
    def __init__(self, ttl_seconds: int):
        self.ttl = ttl_seconds
        self.items = {}
        self.lock = threading.Lock()

    def issue(self) -> str:
        tok = secrets.token_urlsafe(24)
        with self.lock:
            self.items[tok] = time.time() + self.ttl
        return tok

    def valid(self, tok: str) -> bool:
        if not tok:
            return False
        now = time.time()
        with self.lock:
            exp = self.items.get(tok)
            if exp is None:
                return False
            if exp < now:
                del self.items[tok]
                return False
            return True

    def sweep(self):
        now = time.time()
        with self.lock:
            for k in [k for k, v in self.items.items() if v < now]:
                del self.items[k]


# --------------------------------------------------------------------- #
#  Audit
# --------------------------------------------------------------------- #

class Audit:
    def __init__(self, path):
        self.path = path
        self.lock = threading.Lock()

    def write(self, **fields):
        fields["ts"] = time.strftime("%Y-%m-%dT%H:%M:%S%z")
        line = json.dumps(fields, separators=(",", ":"))
        if self.path:
            with self.lock:
                with open(self.path, "a") as fh:
                    fh.write(line + "\n")
        else:
            print("  audit " + line, flush=True)


# --------------------------------------------------------------------- #
#  Login page
# --------------------------------------------------------------------- #

LOGIN_HTML = """<!doctype html><html><head><meta charset=utf-8>
<meta name=viewport content="width=device-width,initial-scale=1">
<title>PulseFeed Bridge</title><style>
*{box-sizing:border-box}body{margin:0;min-height:100vh;display:grid;place-items:center;
background:radial-gradient(900px 500px at 50%% 0,#16202e,#070b11);color:#e8eef7;
font:15px/1.5 ui-sans-serif,system-ui,-apple-system,Segoe UI,Roboto,sans-serif;padding:20px}
.box{width:min(380px,100%%);background:#141b26;border:1px solid #26364c;border-radius:18px;
padding:26px;text-align:center}
.logo{width:46px;height:46px;margin:0 auto 14px;border-radius:12px;display:grid;place-items:center;
background:conic-gradient(from 210deg,#35c6f4,#4fe3a8,#35c6f4);color:#06121b;font-weight:800}
h1{font-size:20px;margin:0 0 4px}p{color:#7d8fa6;font-size:13px;margin:6px 0 16px}
input{width:100%%;background:#0b111b;border:1px solid #2a3646;border-radius:11px;
padding:13px;color:inherit;font:inherit;text-align:center;letter-spacing:.12em}
button{width:100%%;margin-top:10px;border:0;border-radius:11px;padding:13px;
background:#4fe3a8;color:#06121b;font-weight:800;font-size:15px;cursor:pointer}
.err{color:#ff4d5a;font-size:13px;min-height:18px;margin-top:8px}
.mode{display:inline-block;margin-top:14px;font-size:11px;letter-spacing:.09em;
text-transform:uppercase;color:#7d8fa6;border:1px solid #2a3646;border-radius:999px;padding:5px 11px}
</style></head><body><div class=box>
<div class=logo>PF</div>
<h1>PulseFeed Bridge</h1>
<p>This link is protected. Enter the share key you were given.</p>
<form method=POST action=/bridge/login>
<input name=key type=password placeholder="share key" autofocus autocomplete=off>
<button type=submit>UNLOCK</button></form>
<div class=err>%(err)s</div>
<div class=mode>mode: %(mode)s</div>
</div></body></html>"""


# --------------------------------------------------------------------- #
#  Handler
# --------------------------------------------------------------------- #

class BridgeHandler(BaseHTTPRequestHandler):
    server_version = f"PulseFeedBridge/{VERSION}"
    protocol_version = "HTTP/1.1"

    cfg = None          # set on the server instance

    # ---- plumbing ----------------------------------------------------
    def log_message(self, fmt, *args):
        if self.cfg.verbose:
            sys.stderr.write("  %s %s\n" % (self.client_address[0], fmt % args))

    def _client(self):
        # Trust X-Forwarded-For only when the immediate peer is a
        # configured trusted proxy; otherwise it is attacker-controlled.
        peer = self.client_address[0]
        if peer in self.cfg.trusted_proxies:
            xff = self.headers.get("X-Forwarded-For", "")
            if xff:
                return xff.split(",")[0].strip()
        return peer

    def _send(self, code, body=b"", ctype="text/plain; charset=utf-8", headers=None):
        if isinstance(body, str):
            body = body.encode()
        self.send_response(code)
        self.send_header("Content-Type", ctype)
        self.send_header("Content-Length", str(len(body)))
        self.send_header("X-Content-Type-Options", "nosniff")
        self.send_header("Referrer-Policy", "no-referrer")
        self.send_header("X-Frame-Options", "DENY")
        for k, v in (headers or {}):
            self.send_header(k, v)
        self.end_headers()
        try:
            self.wfile.write(body)
        except (BrokenPipeError, ConnectionResetError):
            pass

    def _json(self, code, obj):
        self._send(code, json.dumps(obj), "application/json")

    def _cookie(self):
        raw = self.headers.get("Cookie", "")
        for part in raw.split(";"):
            if "=" in part:
                k, v = part.strip().split("=", 1)
                if k == "pf_bridge":
                    return v
        return ""

    # ---- access control ----------------------------------------------
    def _cidr_ok(self, addr) -> bool:
        if not self.cfg.allow_nets:
            return True
        try:
            ip = ipaddress.ip_address(addr)
        except ValueError:
            return False
        return any(ip in net for net in self.cfg.allow_nets)

    def _authed(self) -> bool:
        if not self.cfg.share_key:
            return True
        return self.cfg.sessions.valid(self._cookie())

    # ---- policy ------------------------------------------------------
    def _policy_check(self, path, args):
        """Return None if allowed, else (code, message)."""
        mode = self.cfg.mode

        if path in BLOCKED_ALWAYS:
            return 403, "blocked_by_bridge"
        if path in BLOCKED_UNLESS_FULL and mode != "full":
            return 403, f"requires_mode_full (bridge is '{mode}')"

        if path == "/api/v1/control":
            action = args.get("action", [""])[0]
            if action not in CONTROL_ACTIONS[mode]:
                return 403, f"action '{action}' not permitted in mode '{mode}'"
            return None

        if path == "/api/v1/settings":
            allowed = SETTINGS_KEYS[mode]
            if allowed is None:
                return None
            if not allowed:
                return 403, f"settings are read-only in mode '{mode}'"
            bad = [k for k in args if k not in allowed]
            if bad:
                return 403, f"cannot set {bad} in mode '{mode}'"
            if mode == "supervise":
                # Only permit changes that reduce output. Anyone watching
                # remotely can turn things down; turning them up is a
                # decision for whoever is standing at the machine.
                err = self._supervise_reduces_only(args)
                if err:
                    return 403, err
            return None

        if path in ("/api/v1/rhythm", "/api/v1/custom"):
            if mode in ("readonly", "supervise"):
                return 403, f"pattern changes not permitted in mode '{mode}'"
            return None

        return None

    def _supervise_reduces_only(self, args):
        try:
            state = self._fetch_state()
        except Exception:
            return "cannot verify current state; refusing in supervise mode"
        for key in ("vac", "ppm", "motor"):
            if key in args:
                try:
                    new = int(args[key][0])
                except (ValueError, IndexError):
                    return f"bad value for {key}"
                cur = int(state.get(key, 0))
                if new > cur:
                    return (f"supervise mode may only reduce {key} "
                            f"(current {cur}, requested {new})")
        return None

    def _fetch_state(self):
        req = urllib.request.Request(self.cfg.target + "/api/v1/state")
        for k, v in self.cfg.device_headers.items():
            req.add_header(k, v)
        with urllib.request.urlopen(req, timeout=self.cfg.timeout) as r:
            return json.loads(r.read().decode())

    # ---- proxy -------------------------------------------------------
    def _forward(self, method, path, query, body):
        url = self.cfg.target + path + (("?" + query) if query else "")
        req = urllib.request.Request(url, data=body, method=method)

        # Forward only what the device needs. Everything else -- Host,
        # Cookie, X-Forwarded-*, the bridge's own session -- is dropped.
        for h in ("Content-Type", "Accept", "X-PF-Token"):
            v = self.headers.get(h)
            if v:
                req.add_header(h, v)
        for k, v in self.cfg.device_headers.items():
            req.add_header(k, v)

        try:
            with urllib.request.urlopen(req, timeout=self.cfg.timeout) as r:
                payload = r.read()
                ctype = r.headers.get("Content-Type", "application/octet-stream")
                enc = r.headers.get("Content-Encoding")
                extra = [("Content-Encoding", enc)] if enc else None
                self._send(r.status, payload, ctype, extra)
        except urllib.error.HTTPError as e:
            payload = e.read()
            self._send(e.code, payload, e.headers.get("Content-Type", "application/json"))
        except urllib.error.URLError as e:
            self._json(502, {"error": "device_unreachable", "detail": str(e.reason)})
        except TimeoutError:
            self._json(504, {"error": "device_timeout"})

    # ---- entry points ------------------------------------------------
    def do_GET(self):
        self._handle("GET")

    def do_POST(self):
        self._handle("POST")

    def do_HEAD(self):
        self._handle("GET")

    def _handle(self, method):
        parsed = urllib.parse.urlparse(self.path)
        path, query = parsed.path, parsed.query
        client = self._client()

        if path == "/healthz":
            self._json(200, {"ok": True, "bridge": VERSION, "mode": self.cfg.mode})
            return

        if not self._cidr_ok(client):
            self.cfg.audit.write(event="cidr_reject", ip=client, path=path)
            self._json(403, {"error": "address_not_allowed"})
            return

        if not self.cfg.limiter.allow(client):
            self.cfg.audit.write(event="rate_limit", ip=client, path=path)
            self._json(429, {"error": "rate_limited"})
            return

        # ---- bridge login ---------------------------------------------
        if path == "/bridge/login":
            if method == "POST":
                length = int(self.headers.get("Content-Length") or 0)
                form = urllib.parse.parse_qs(self.rfile.read(length).decode())
                given = form.get("key", [""])[0]
                if hmac.compare_digest(given, self.cfg.share_key or ""):
                    tok = self.cfg.sessions.issue()
                    self.cfg.audit.write(event="login_ok", ip=client)
                    secure = "; Secure" if self.cfg.tls else ""
                    self.send_response(303)
                    self.send_header("Location", "/")
                    self.send_header(
                        "Set-Cookie",
                        f"pf_bridge={tok}; Path=/; HttpOnly; SameSite=Strict{secure}")
                    self.send_header("Content-Length", "0")
                    self.end_headers()
                    return
                self.cfg.audit.write(event="login_fail", ip=client)
                time.sleep(0.4)
                self._send(401, LOGIN_HTML % {"err": "Incorrect key", "mode": self.cfg.mode},
                           "text/html; charset=utf-8")
                return
            self._send(200, LOGIN_HTML % {"err": "", "mode": self.cfg.mode},
                       "text/html; charset=utf-8")
            return

        # ---- bridge auth ----------------------------------------------
        if not self._authed() and path not in OPEN_PATHS:
            if path.startswith("/api/"):
                self._json(401, {"error": "bridge_auth_required"})
            else:
                self._send(200, LOGIN_HTML % {"err": "", "mode": self.cfg.mode},
                           "text/html; charset=utf-8")
            return

        # ---- read body -------------------------------------------------
        body = b""
        args = {}
        if method == "POST":
            length = int(self.headers.get("Content-Length") or 0)
            if length > self.cfg.max_body:
                self._json(413, {"error": "body_too_large"})
                return
            body = self.rfile.read(length)
            ctype = self.headers.get("Content-Type", "")
            if "x-www-form-urlencoded" in ctype:
                args = urllib.parse.parse_qs(body.decode("utf-8", "replace"))
        else:
            args = urllib.parse.parse_qs(query)

        # ---- policy ----------------------------------------------------
        if method == "POST" or path == "/api/v1/control":
            verdict = self._policy_check(path, args)
            if verdict:
                code, msg = verdict
                self.cfg.audit.write(event="policy_deny", ip=client, path=path,
                                     mode=self.cfg.mode, reason=msg,
                                     args={k: v[:1] for k, v in args.items() if k != "pin"})
                self._json(code, {"error": "blocked_by_bridge", "detail": msg})
                return

            self.cfg.audit.write(event="forward", ip=client, path=path,
                                 mode=self.cfg.mode,
                                 args={k: v[:1] for k, v in args.items() if k != "pin"})

        self._forward(method, path, query, body if method == "POST" else None)


# --------------------------------------------------------------------- #
#  Config / main
# --------------------------------------------------------------------- #

class Config:
    pass


def main():
    ap = argparse.ArgumentParser(
        description="Safety-gated reverse proxy for a PulseFeed controller.",
        formatter_class=argparse.RawDescriptionHelpFormatter,
        epilog=__doc__)
    ap.add_argument("--target", required=True,
                    help="device base URL, e.g. http://192.168.13.50")
    ap.add_argument("--listen", default="0.0.0.0:8443", help="host:port to bind")
    ap.add_argument("--mode", default="supervise", choices=MODES)
    ap.add_argument("--share-key", default=None,
                    help="key visitors must enter. Generated if omitted; "
                         "pass '' to disable bridge auth entirely (LAN only!)")
    ap.add_argument("--device-token", default=None,
                    help="device API token to inject, so visitors never see the device PIN")
    ap.add_argument("--device-pin", default=None,
                    help="device PIN; the bridge exchanges it for a token at startup")
    ap.add_argument("--allow-cidr", action="append", default=[],
                    help="restrict clients, repeatable, e.g. 192.168.0.0/16")
    ap.add_argument("--trusted-proxy", action="append", default=[],
                    help="peer addresses whose X-Forwarded-For is believed")
    ap.add_argument("--rate", type=int, default=240, help="requests per minute per client")
    ap.add_argument("--timeout", type=float, default=8.0)
    ap.add_argument("--max-body", type=int, default=16384)
    ap.add_argument("--audit", default=None, help="append-only JSONL audit file")
    ap.add_argument("--cert", default=None)
    ap.add_argument("--key", default=None)
    ap.add_argument("--session-ttl", type=int, default=12 * 3600)
    ap.add_argument("--verbose", action="store_true")
    ap.add_argument("--i-understand", action="store_true",
                    help="required for --mode full")
    args = ap.parse_args()

    if args.mode == "full" and not args.i_understand:
        print("refusing --mode full without --i-understand.\n"
              "  full mode lets a remote client START the machine.\n"
              "  Only use it when someone is physically present at the device.",
              file=sys.stderr)
        return 2

    cfg = Config()
    cfg.target = args.target.rstrip("/")
    cfg.mode = args.mode
    cfg.timeout = args.timeout
    cfg.max_body = args.max_body
    cfg.verbose = args.verbose
    cfg.tls = bool(args.cert and args.key)
    cfg.trusted_proxies = set(args.trusted_proxy)
    cfg.limiter = RateLimiter(args.rate)
    cfg.sessions = Sessions(args.session_ttl)
    cfg.audit = Audit(args.audit)

    cfg.allow_nets = []
    for c in args.allow_cidr:
        cfg.allow_nets.append(ipaddress.ip_network(c, strict=False))

    if args.share_key is None:
        cfg.share_key = secrets.token_urlsafe(9)
        generated = True
    else:
        cfg.share_key = args.share_key
        generated = False

    # Exchange the device PIN for a token once, at startup, so the PIN
    # itself never travels beyond this process.
    cfg.device_headers = {}
    token = args.device_token
    if not token and args.device_pin:
        try:
            data = urllib.parse.urlencode({"pin": args.device_pin}).encode()
            req = urllib.request.Request(cfg.target + "/api/v1/auth", data=data, method="POST")
            req.add_header("Content-Type", "application/x-www-form-urlencoded")
            with urllib.request.urlopen(req, timeout=args.timeout) as r:
                token = json.loads(r.read().decode()).get("token")
            print(f"  exchanged device PIN for a token ({token[:6]}...)")
        except Exception as e:
            print(f"  warning: could not authenticate to the device: {e}", file=sys.stderr)
    if token:
        cfg.device_headers["X-PF-Token"] = token

    host, _, port = args.listen.rpartition(":")
    host = host or "0.0.0.0"
    BridgeHandler.cfg = cfg

    httpd = ThreadingHTTPServer((host, int(port)), BridgeHandler)
    httpd.daemon_threads = True

    scheme = "http"
    if cfg.tls:
        ctx = ssl.SSLContext(ssl.PROTOCOL_TLS_SERVER)
        ctx.load_cert_chain(args.cert, args.key)
        ctx.minimum_version = ssl.TLSVersion.TLSv1_2
        httpd.socket = ctx.wrap_socket(httpd.socket, server_side=True)
        scheme = "https"

    def sweeper():
        while True:
            time.sleep(300)
            cfg.sessions.sweep()
    threading.Thread(target=sweeper, daemon=True).start()

    print(f"\n  PulseFeed Bridge {VERSION}")
    print(f"  target   {cfg.target}")
    print(f"  listen   {scheme}://{host}:{port}")
    print(f"  mode     {cfg.mode}   (control actions: "
          f"{', '.join(sorted(CONTROL_ACTIONS[cfg.mode]))})")
    if cfg.share_key:
        print(f"  key      {cfg.share_key}" + ("   <- generated, share this" if generated else ""))
    else:
        print("  key      DISABLED -- anyone who can reach this port has access")
    if cfg.allow_nets:
        print(f"  allow    {', '.join(str(n) for n in cfg.allow_nets)}")
    print(f"  rate     {args.rate}/min per client")
    print(f"  audit    {args.audit or 'stdout'}\n")

    try:
        httpd.serve_forever()
    except KeyboardInterrupt:
        print("\n  stopped")
    return 0


if __name__ == "__main__":
    sys.exit(main())
