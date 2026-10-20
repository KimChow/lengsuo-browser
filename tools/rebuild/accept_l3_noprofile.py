#!/usr/bin/env python3
"""Acceptance for the "no profile" case: upstream behaviour must be unchanged.

When no L3 document is supplied, the browser must behave exactly like a stock
Chromium build: nothing is pinned, every key is writable, and clear() wipes
everything. A fingerprint browser that changes behaviour without a profile
would itself be a fingerprint.

This is the counterpart of accept_l3.py, which covers the pinned case.

Usage:
    python3 tools/rebuild/accept_l3_noprofile.py [--chrome <path>]
"""

import argparse
import http.server
import json
import pathlib
import shutil
import socket
import subprocess
import tempfile
import threading
import time

PAGE = """<!doctype html>
<html><head><meta charset="utf-8"><title>pending</title></head>
<body>
<script>
(function () {
  var out = {};
  try {
    out.read1 = localStorage.getItem('finger-key_0.1.5');
    localStorage.setItem('free-key', 'free-value');
    out.free = localStorage.getItem('free-key');
    localStorage.setItem('finger-key_0.1.5', 'HACKED');
    out.read2 = localStorage.getItem('finger-key_0.1.5');
    localStorage.removeItem('finger-key_0.1.5');
    out.read3 = localStorage.getItem('finger-key_0.1.5');
    localStorage.clear();
    out.read4 = localStorage.getItem('finger-key_0.1.5');
    out.freeAfterClear = localStorage.getItem('free-key');
    out.error = null;
  } catch (e) {
    out.error = String(e);
  }
  document.title = 'L3RESULT' + JSON.stringify(out);
})();
</script>
</body></html>
"""


def free_port() -> int:
    with socket.socket() as s:
        s.bind(("127.0.0.1", 0))
        return s.getsockname()[1]


class Handler(http.server.BaseHTTPRequestHandler):
    def do_GET(self):  # noqa: N802
        body = PAGE.encode("utf-8")
        self.send_response(200)
        self.send_header("Content-Type", "text/html; charset=utf-8")
        self.send_header("Content-Length", str(len(body)))
        self.end_headers()
        self.wfile.write(body)

    def log_message(self, *args):
        pass


def main() -> int:
    parser = argparse.ArgumentParser()
    parser.add_argument("--chrome",
                        default="/home/ubuntu/lengsuo-browser-dev/src/out/lengsuo/chrome")
    args = parser.parse_args()

    chrome = pathlib.Path(args.chrome)
    if not chrome.exists():
        print(f"chrome not found: {chrome}")
        return 2

    port = free_port()
    server = http.server.ThreadingHTTPServer(("127.0.0.1", port), Handler)
    threading.Thread(target=server.serve_forever, daemon=True).start()

    work = pathlib.Path(tempfile.mkdtemp(prefix="l3-noprofile-"))
    profile = work / "profile"
    profile.mkdir()

    cmd = [
        str(chrome),
        "--headless=new",
        "--no-sandbox",
        "--disable-gpu",
        f"--user-data-dir={profile}",
        "--virtual-time-budget=8000",
        "--dump-dom",
        f"http://127.0.0.1:{port}/",
    ]
    print("launching without --lengsuo-l3-wire ...")
    try:
        done = subprocess.run(cmd, capture_output=True, text=True, timeout=120)
    except subprocess.TimeoutExpired:
        print("browser timed out")
        return 2
    finally:
        server.shutdown()

    dom = done.stdout
    marker = "L3RESULT"
    if marker not in dom:
        print("FAIL: page never produced a result")
        print("stdout head:", dom[:800])
        print("stderr head:", done.stderr[:800])
        shutil.rmtree(work, ignore_errors=True)
        return 1
    raw = dom.split(marker, 1)[1].split("</title>")[0]
    payload = json.loads(raw.replace("&quot;", '"'))

    print("\n=== L3 no-profile acceptance (upstream behaviour must not change) ===")
    checks = [
        ("nothing is pinned (key absent)",
         payload.get("read1") is None, payload.get("read1")),
        ("non-pinned key is writable",
         payload.get("free") == "free-value", payload.get("free")),
        ("any key can be overwritten",
         payload.get("read2") == "HACKED", payload.get("read2")),
        ("any key can be removed",
         payload.get("read3") is None, payload.get("read3")),
        ("clear() wipes everything",
         payload.get("read4") is None, payload.get("read4")),
        ("clear() removed the other key too",
         payload.get("freeAfterClear") is None, payload.get("freeAfterClear")),
        ("no script error", payload.get("error") is None, payload.get("error")),
    ]
    ok = True
    for name, passed, actual in checks:
        print(f"  [{'PASS' if passed else 'FAIL'}] {name}  (got {actual!r})")
        ok = ok and passed
    shutil.rmtree(work, ignore_errors=True)
    print("RESULT:", "ALL PASS" if ok else "FAILURES")
    return 0 if ok else 1


if __name__ == "__main__":
    raise SystemExit(main())
