#!/usr/bin/env python3
"""serve.py — serves a built Sim Studio site (sim/studio/build.py) with the Python standard library.

    python3 sim/studio/serve.py [--dir build/studio] [--host 127.0.0.1] [--port 8765] [--tailnet] [--build]

  --build     run sim/studio/build.py into --dir first (only from a checkout)
  --tailnet   bind to this machine's Tailscale IPv4 (`tailscale ip -4`) instead of --host; waits until Tailscale has an
              address (so a LaunchAgent started before tailscaled is up does not crash-loop). Tailscale is only read.

Static files only: .wasm is served as application/wasm, and .wasm/.js/.html/.json/.css with "Cache-Control: no-cache"
(a rebuilt site is picked up on reload). Nothing is written; the simulator runs entirely in the browser.
Python 3.8+, stdlib only.
"""
import argparse
import functools
import os
import shutil
import subprocess
import sys
import time
from http.server import SimpleHTTPRequestHandler, ThreadingHTTPServer
from pathlib import Path

HERE = Path(__file__).resolve().parent
NO_CACHE = (".wasm", ".js", ".mjs", ".html", ".json", ".css", "/")


class Handler(SimpleHTTPRequestHandler):
    extensions_map = {**SimpleHTTPRequestHandler.extensions_map,
                      ".wasm": "application/wasm", ".js": "text/javascript", ".mjs": "text/javascript",
                      ".json": "application/json", ".css": "text/css", ".svg": "image/svg+xml"}

    def end_headers(self):
        path = self.path.split("?", 1)[0]
        if path.endswith(NO_CACHE):
            self.send_header("Cache-Control", "no-cache")
        self.send_header("X-Content-Type-Options", "nosniff")
        super().end_headers()

    def log_message(self, fmt, *args):     # one short line per request (LaunchAgent log)
        sys.stderr.write("%s %s\n" % (time.strftime("%Y-%m-%d %H:%M:%S"), fmt % args))


def tailscale_ip(wait=True):
    exe = shutil.which("tailscale") or next((p for p in ("/opt/homebrew/bin/tailscale", "/usr/local/bin/tailscale",
                                             "/Applications/Tailscale.app/Contents/MacOS/Tailscale") if os.path.exists(p)), None)
    if not exe:
        raise SystemExit("serve.py: --tailnet: tailscale CLI not found")
    delay = 2
    while True:
        try:
            out = subprocess.run([exe, "ip", "-4"], capture_output=True, text=True, timeout=10).stdout.split()
            if out and out[0].startswith("100."):
                return out[0]
        except (OSError, subprocess.TimeoutExpired):
            pass
        if not wait:
            raise SystemExit("serve.py: --tailnet: no Tailscale IPv4 address")
        sys.stderr.write(f"serve.py: waiting for a Tailscale address (retry in {delay} s)\n")
        time.sleep(delay)
        delay = min(30, delay * 2)


def main():
    ap = argparse.ArgumentParser(description=__doc__.split("\n\n")[0])
    ap.add_argument("--dir", default=str(HERE.parent.parent / "build" / "studio"), help="site dir (default build/studio)")
    ap.add_argument("--host", default="127.0.0.1")
    ap.add_argument("--port", type=int, default=8765)
    ap.add_argument("--tailnet", action="store_true", help="bind to `tailscale ip -4`")
    ap.add_argument("--build", action="store_true", help="run sim/studio/build.py --out DIR first")
    ap.add_argument("--no-classic", action="store_true", help="with --build: skip the classic build")
    a = ap.parse_args()
    site = Path(a.dir).resolve()
    if a.build:
        cmd = [sys.executable, str(HERE / "build.py"), "--out", str(site)] + (["--no-classic"] if a.no_classic else [])
        if subprocess.run(cmd).returncode != 0:
            raise SystemExit(1)
    if not (site / "index.html").exists():
        raise SystemExit(f"serve.py: {site} has no index.html — build it first (sim/studio.sh or --build)")
    host = tailscale_ip() if a.tailnet else a.host
    httpd = ThreadingHTTPServer((host, a.port), functools.partial(Handler, directory=str(site)))
    shown = f"[{host}]" if ":" in host else host
    print(f"Sim Studio: http://{shown}:{a.port}/   (serving {site}; Ctrl-C stops)", flush=True)
    try:
        httpd.serve_forever()
    except KeyboardInterrupt:
        pass


if __name__ == "__main__":
    main()
