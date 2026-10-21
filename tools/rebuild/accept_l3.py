#!/usr/bin/env python3
"""End-to-end acceptance for the L3 storage layer.

Starts a throwaway HTTP origin, writes a signed L3 document that pins one
localStorage key on that origin, launches the freshly built chrome with
--lengsuo-l3-wire, and checks the four behaviours that matter:

  1. the pinned value is visible to the first script that reads it
  2. a page cannot overwrite the pinned value
  3. a page cannot remove the pinned value
  4. clear() wipes the other keys but the pinned value survives

Everything runs on a temporary port against a temporary profile; nothing
outside the temp directory is touched.

Usage:
    python3 tools/rebuild/accept_l3.py [--chrome <path>] [--keep]
"""

import argparse
import http.server
import json
import os
import pathlib
import shutil
import socket
import subprocess
import sys
import tempfile
import threading
import time

REPO = pathlib.Path(__file__).resolve().parents[2]

# The page does the probing and writes the result into document.title so
# --dump-dom can carry it out.
PAGE = """<!doctype html>
<html><head><meta charset="utf-8"><title>pending</title></head>
<body>
<script>
(function () {
  var out = {};
  try {
    out.read1 = localStorage.getItem('finger-key_0.1.5');
    // A non-pinned key must stay writable, otherwise the pinning would look
    // like the whole storage area is frozen.
    localStorage.setItem('free-key', 'free-value');
    out.free = localStorage.getItem('free-key');
    // Overwriting the pinned key must be refused silently (no throw).
    localStorage.setItem('finger-key_0.1.5', 'HACKED');
    out.read2 = localStorage.getItem('finger-key_0.1.5');
    // Removing it must be refused too.
    localStorage.removeItem('finger-key_0.1.5');
    out.read3 = localStorage.getItem('finger-key_0.1.5');
    // clear() must keep the pinned key but wipe the rest.
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
    """Records renderer command lines while this browser run is alive.

    Task O moved the L3 document off the command line, so the absence of
    ``--lengsuo-managed-l3`` on a *renderer* is now a contract, not an
    implementation detail. The profile path is the process selector: it is
    unique to this run and Chromium copies --user-data-dir to children, so
    another browser on the host can neither satisfy nor break the check.
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
                continue  # process exited between listing and reading
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


def make_document(origin: str, key: str, value: str, timestamp: int) -> str:
    """Build the L3 document with the production encoder.

    The document used to be hand-assembled here, with its own MD5 call. That
    is a second implementation of the wire format: if the production encoder
    ever changes, the acceptance run would keep testing the old shape and
    pass. Now the document is produced by the real EncodeL3Wire through a
    tiny helper binary (tools/rebuild/make_l3_sample), so the bytes under
    test are the bytes the browser will parse.

    The cookie domain has to match the origin host, because the consistency
    gate treats "cookie declared but storage present and unequal" as a hard
    mismatch. Passing the bare host keeps the mirror rule applicable.
    """
    helper = pathlib.Path(tempfile.gettempdir()) / "make_l3_sample"
    if not helper.exists():
        print(f"helper not built: {helper}")
        print("build it first:")
        print("  g++ -std=c++20 -I native tools/rebuild/make_l3_sample.cc \\")
        print("      native/l3_wire.cc native/storage_pin.cc \\")
        print("      native/storage_consistency.cc native/cache_record.cc \\")
        print("      native/extension_pin.cc -o /tmp/make_l3_sample")
        raise SystemExit(2)
    host = origin.split("://", 1)[-1].split("/", 1)[0].split(":", 1)[0]
    out = pathlib.Path(tempfile.mkdtemp(prefix="l3-doc-")) / "l3.wire"
    subprocess.run(
        [str(helper), str(out), str(timestamp), origin, key, value, host, "_bee"],
        check=True, capture_output=True)
    text = out.read_text(encoding="utf-8")
    shutil.rmtree(out.parent, ignore_errors=True)
    return text


def main() -> int:
    parser = argparse.ArgumentParser()
    parser.add_argument("--chrome", default="/home/ubuntu/lengsuo-browser-dev/src/out/lengsuo/chrome")
    parser.add_argument("--keep", action="store_true",
                        help="keep the temporary profile for inspection")
    args = parser.parse_args()

    chrome = pathlib.Path(args.chrome)
    if not chrome.exists():
        print(f"chrome not found: {chrome}")
        return 2

    port = free_port()
    origin = f"http://127.0.0.1:{port}"
    key = "finger-key_0.1.5"
    value = "7FcROXwfKGAKuwlRwwlBCEUPeZXjwrH5"

    server = http.server.ThreadingHTTPServer(("127.0.0.1", port), Handler)
    threading.Thread(target=server.serve_forever, daemon=True).start()

    work = pathlib.Path(tempfile.mkdtemp(prefix="l3-accept-"))
    doc_path = work / "l3.txt"
    doc_path.write_text(make_document(origin, key, value, int(time.time())),
                        encoding="utf-8")
    profile = work / "profile"
    profile.mkdir()

    cmd = [
        str(chrome),
        "--headless=new",
        "--no-sandbox",
        "--disable-gpu",
        f"--user-data-dir={profile}",
        f"--lengsuo-l3-wire={doc_path}",
        "--virtual-time-budget=8000",
        "--dump-dom",
        f"http://127.0.0.1:{port}/",
    ]
    print("launching:", " ".join(cmd[:6]), "...")
    try:
        with RendererCmdlineWatcher(profile) as watcher:
            done = subprocess.run(cmd, capture_output=True, text=True,
                                  timeout=120)
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
        if not args.keep:
            shutil.rmtree(work, ignore_errors=True)
        return 1
    # The result rides in <title> as "L3RESULT{...}". Take everything from
    # the marker up to the closing tag and let the JSON decoder find the
    # object boundaries. The earlier version split on the first double quote,
    # which is the opening quote of the JSON object itself, so it parsed "{"
    # and died.
    tail = dom.split(marker, 1)[1]
    end = tail.find("</title>")
    blob = tail[:end] if end >= 0 else tail
    blob = blob.replace("&quot;", '"').replace("&amp;", "&")
    try:
        payload = json.loads(blob)
    except json.JSONDecodeError as error:
        print(f"FAIL: result payload is not valid JSON: {error}")
        print("payload:", blob[:600])
        if not args.keep:
            shutil.rmtree(work, ignore_errors=True)
        return 1

    print("\n=== L3 end-to-end acceptance ===")
    print("origin:", origin)
    checks = [
        ("pinned value visible to the first script",
         payload.get("read1") == value, payload.get("read1")),
        ("non-pinned key still writable",
         payload.get("free") == "free-value", payload.get("free")),
        ("pinned key cannot be overwritten",
         payload.get("read2") == value, payload.get("read2")),
        ("pinned key cannot be removed",
         payload.get("read3") == value, payload.get("read3")),
        ("clear() keeps the pinned key",
         payload.get("read4") == value, payload.get("read4")),
        ("clear() wipes the other keys",
         payload.get("freeAfterClear") is None, payload.get("freeAfterClear")),
        ("no script error", payload.get("error") is None, payload.get("error")),
    ]
    ok = True
    for name, passed, actual in checks:
        print(f"  [{'PASS' if passed else 'FAIL'}] {name}  (got {actual!r})")
        ok = ok and passed

    # Task O contract: the document reaches the renderer over Mojo, so it can
    # never show up in a renderer's argv. Failure to observe a renderer makes
    # the check vacuous, which counts as a failure rather than a pass.
    leaked = [pid for pid, cmd in watcher.cmdlines
              if b"lengsuo-managed-l3" in cmd]
    passed = bool(watcher.cmdlines) and not leaked
    print(f"  [{'PASS' if passed else 'FAIL'}] renderer cmdline carries no "
          f"lengsuo-managed-l3  ({len(watcher.cmdlines)} renderers observed, "
          f"{len(leaked)} leaked)")
    ok = ok and passed

    if args.keep:
        print("temp dir kept at", work)
    else:
        shutil.rmtree(work, ignore_errors=True)
    print("RESULT:", "ALL PASS" if ok else "FAILURES PRESENT")
    return 0 if ok else 1


if __name__ == "__main__":
    raise SystemExit(main())
