"""Serve the fingerprint probe and collect the result the page reports itself.

Why not CDP/Playwright: those transports attach automation to the browser
(`--enable-automation`, a debugging port), and ``navigator.webdriver`` then
reports the harness rather than the browser under test. Here the browser is
launched exactly as a user would launch it, merely pointed at this URL; the page
posts its own reading back, so every value is one a real site would see.

Both variants of a comparison must go through this same server so the transport
contributes nothing to the diff.

Usage::

    python tools/fp_report_server.py --port 8791 --expect-label ours-default
"""

from __future__ import annotations

import argparse
import json
import pathlib
import sys
import threading
from http.server import BaseHTTPRequestHandler, ThreadingHTTPServer

ROOT = pathlib.Path(__file__).resolve().parents[1]
PROBE = ROOT / "tests" / "fp-probe.html"
# Served at /sw.js so the page can read the service-worker-side navigator: a
# service worker has its own, and overrides applied elsewhere do not reach it.
SERVICE_WORKER = ROOT / "tests" / "sw.js"
INDEX = (b"<!doctype html><meta charset=utf-8><title>probe</title>"
         b"<pre id=out>open /fp-probe.html?report=1</pre>")

_result: dict | None = None
_event = threading.Event()


class Handler(BaseHTTPRequestHandler):
    protocol_version = "HTTP/1.1"

    def log_message(self, fmt, *args):  # keep the console readable, but show hits
        sys.stderr.write("[%s] %s\n" % (self.command, self.path))
        sys.stderr.flush()

    def _send(self, code: int, body: bytes, ctype: str) -> None:
        self.send_response(code)
        self.send_header("Content-Type", ctype)
        self.send_header("Content-Length", str(len(body)))
        self.end_headers()
        self.wfile.write(body)

    def do_GET(self):  # noqa: N802 - http.server API
        path = self.path.split("?", 1)[0]
        if path in ("/fp-probe.html", "/"):
            self._send(200, PROBE.read_bytes(), "text/html; charset=utf-8")
        elif path == "/sw.js":
            # A service worker must be served with a JS content type and from
            # the same origin, or registration fails.
            self._send(200, SERVICE_WORKER.read_bytes(),
                       "text/javascript; charset=utf-8")
        else:
            self._send(200, INDEX, "text/html; charset=utf-8")

    def do_POST(self):  # noqa: N802 - http.server API
        global _result
        if self.path != "/report":
            self._send(404, b"no", "text/plain")
            return
        length = int(self.headers.get("Content-Length") or 0)
        raw = self.rfile.read(length)
        try:
            _result = json.loads(raw.decode("utf-8"))
        except Exception as exc:  # noqa: BLE001
            _result = {"error": "bad payload: %s" % exc}
        _event.set()
        self._send(200, b'{"ok":true}', "application/json")


def main(argv: list[str]) -> int:
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("--port", type=int, default=8791)
    parser.add_argument("--expect-label", required=True)
    parser.add_argument("--out", required=True)
    parser.add_argument("--timeout", type=float, default=120.0)
    args = parser.parse_args(argv)

    server = ThreadingHTTPServer(("127.0.0.1", args.port), Handler)
    thread = threading.Thread(target=server.serve_forever, daemon=True)
    thread.start()
    print("serving on http://127.0.0.1:%d  (waiting for %s)"
          % (args.port, args.expect_label), flush=True)

    if not _event.wait(args.timeout):
        server.shutdown()
        print("timed out after %.0fs without a report" % args.timeout,
              file=sys.stderr)
        return 1

    server.shutdown()
    payload = {
        "label": args.expect_label,
        "transport": "page-self-report",
        "probe_url": "http://127.0.0.1:%d/fp-probe.html?report=1" % args.port,
        "value": _result,
    }
    out = pathlib.Path(args.out)
    out.parent.mkdir(parents=True, exist_ok=True)
    out.write_text(
        json.dumps(payload, indent=2, ensure_ascii=False, sort_keys=True) + "\n",
        encoding="utf-8",
    )
    print("wrote %s (%s)" % (out, args.expect_label))
    return 0


if __name__ == "__main__":
    raise SystemExit(main(sys.argv[1:]))
