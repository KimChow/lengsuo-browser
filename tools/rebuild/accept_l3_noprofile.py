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
import os
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


class RendererCmdlineWatcher:
    """Records this run's renderer command lines (see accept_l3.py).

    Kept in both scripts rather than a shared module: each acceptance script
    stays runnable on its own, which is how they are invoked in CI.
    """

    def __init__(self, profile: pathlib.Path):
        self._needle = os.fsencode(f"--user-data-dir={profile}")
        # (pid, cmdline) pairs; the same renderer is seen on every poll.
        self.cmdlines: list[tuple[str, bytes]] = []
        self._seen: set[str] = set()
        self._stop = threading.Event()
        self._thread = threading.Thread(target=self._run, daemon=True)

    def _run(self) -> None:
        while not self._stop.is_set():
            self._sample()
            self._stop.wait(0.02)

    def _sample(self) -> None:
        for entry in pathlib.Path("/proc").iterdir():
            if not entry.name.isdigit():
                continue
            try:
                cmdline = (entry / "cmdline").read_bytes()
            except OSError:
                continue
            if b"--type=renderer" in cmdline and self._needle in cmdline:
                if entry.name not in self._seen:
                    self._seen.add(entry.name)
                    self.cmdlines.append((entry.name, cmdline))

    def __enter__(self):
        self._thread.start()
        return self

    def __exit__(self, *exc):
        self._stop.set()
        self._thread.join()


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
        with RendererCmdlineWatcher(profile) as watcher:
            # 300 s, not 120: this host is shared with other lines, and a
            # load spike (load average 67 on 32 cores, measured 2026-10-24)
            # pushes a healthy run past 120 s. The browser finished normally
            # in 130 s on that run and produced the right DOM, so 120 s turned
            # a pass into a false failure. The bound only has to catch a hang.
            done = subprocess.run(cmd, capture_output=True, text=True,
                                  timeout=300)
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

    # Same task-O contract as accept_l3.py: no L3 document, no L3 switch, and
    # certainly none on a renderer's command line.
    leaked = [pid for pid, cmd in watcher.cmdlines
              if b"lengsuo-managed-l3" in cmd]
    passed = bool(watcher.cmdlines) and not leaked
    print(f"  [{'PASS' if passed else 'FAIL'}] renderer cmdline carries no "
          f"lengsuo-managed-l3  ({len(watcher.cmdlines)} renderers observed, "
          f"{len(leaked)} leaked)")
    ok = ok and passed
    shutil.rmtree(work, ignore_errors=True)
    print("RESULT:", "ALL PASS" if ok else "FAILURES")
    return 0 if ok else 1


if __name__ == "__main__":
    raise SystemExit(main())
