"""Collect proven-languages from real browsers across several environments.

The open question is whether the fallback tag in ``navigator.languages``
("en" following "en-US") is *derived* from the locale or *stored* as a
preference list:

  * if derived, we can reproduce it inside the kernel;
  * if stored, the server has to send the list in the profile, and the kernel
    only needs to pass it through.

Guessing which one it is produced two wrong designs already, so this script
answers it with measurements: it launches one real browser per environment and
records ``navigator.language``, ``navigator.languages``, the resolved Intl
locale and the Accept-Language header the browser actually sends.

Chromium derives the header from the same preference list it derives
``navigator.languages`` from (``RendererPreferences::accept_languages``), so
comparing the two tells us where the list comes from. Passing ``--lang``
simulates "a different UI language on this machine" without needing a second
machine: it is the single knob Chromium exposes for that.

Usage::

    python tests/lang_matrix_probe.py --binary /usr/bin/google-chrome-stable
    python tests/lang_matrix_probe.py --binary ./out/lengsuo/chrome --envs "en_US.UTF-8,zh_CN.UTF-8"

Output is one JSON record per (environment, lang) pair plus a table.
"""

from __future__ import annotations

import argparse
import http.server
import json
import os
import pathlib
import subprocess
import tempfile
import threading
import time

ROOT = pathlib.Path(__file__).resolve().parents[1]


def _find_probe() -> pathlib.Path:
    """Locate lang-probe.html whether the script sits in a repo or was copied
    to a scratch directory on the build host."""
    candidates = [
        ROOT / "tests" / "lang-probe.html",
        pathlib.Path(__file__).resolve().parent / "lang-probe.html",
        pathlib.Path.cwd() / "lang-probe.html",
    ]
    for candidate in candidates:
        if candidate.is_file():
            return candidate
    raise SystemExit("lang-probe.html not found; looked in: %s"
                     % ", ".join(str(c) for c in candidates))


PROBE = _find_probe()

# locale -> the env vars a Linux desktop session would carry, plus the --lang
# value. Using real locale names keeps the comparison honest.
ENVIRONMENTS = {
    "en_US": {"LANG": "en_US.UTF-8", "LC_ALL": "en_US.UTF-8", "lang": "en-US"},
    "en_GB": {"LANG": "en_GB.UTF-8", "LC_ALL": "en_GB.UTF-8", "lang": "en-GB"},
    "zh_CN": {"LANG": "zh_CN.UTF-8", "LC_ALL": "zh_CN.UTF-8", "lang": "zh-CN"},
    "ja_JP": {"LANG": "ja_JP.UTF-8", "LC_ALL": "ja_JP.UTF-8", "lang": "ja"},
    "de_DE": {"LANG": "de_DE.UTF-8", "LC_ALL": "de_DE.UTF-8", "lang": "de"},
    "C": {"LANG": "C.UTF-8", "LC_ALL": "C.UTF-8", "lang": "en-US"},
}

RESULTS: list[dict] = []


class Handler(http.server.BaseHTTPRequestHandler):
    def log_message(self, *args):  # keep the console readable
        pass

    def do_GET(self):  # noqa: N802 - http.server API
        body = PROBE.read_bytes()
        self.send_response(200)
        self.send_header("Content-Type", "text/html; charset=utf-8")
        self.send_header("Content-Length", str(len(body)))
        self.end_headers()
        self.wfile.write(body)

    def do_POST(self):  # noqa: N802 - http.server API
        length = int(self.headers.get("Content-Length", 0))
        payload = json.loads(self.rfile.read(length) or b"{}")
        payload["acceptLanguage"] = self.headers.get("Accept-Language")
        payload["secChUaLang"] = self.headers.get("Sec-CH-UA")
        RESULTS.append(payload)
        self.send_response(200)
        self.send_header("Content-Type", "text/plain")
        self.send_header("Content-Length", "2")
        self.end_headers()
        self.wfile.write(b"ok")


def serve(port: int) -> http.server.ThreadingHTTPServer:
    server = http.server.ThreadingHTTPServer(("127.0.0.1", port), Handler)
    threading.Thread(target=server.serve_forever, daemon=True).start()
    return server


def run_one(binary: str, env_name: str, spec: dict, port: int,
            budget_ms: int, headed: bool) -> dict | None:
    before = len(RESULTS)
    profile = tempfile.mkdtemp(prefix="lq-lang-")
    env = dict(os.environ)
    env.update({k: v for k, v in spec.items() if k != "lang"})
    command = [binary, "--no-sandbox", "--disable-dev-shm-usage",
               "--no-first-run", "--no-default-browser-check",
               "--password-store=basic", f"--user-data-dir={profile}",
               f"--lang={spec['lang']}", f"--virtual-time-budget={budget_ms}"]
    if not headed:
        command.append("--headless=new")
    command.append(f"http://127.0.0.1:{port}/lang-probe.html")
    try:
        subprocess.run(command, env=env, timeout=90, capture_output=True)
    except subprocess.TimeoutExpired:
        pass
    for _ in range(20):
        if len(RESULTS) > before:
            break
        time.sleep(0.5)
    if len(RESULTS) <= before:
        return None
    record = RESULTS[before]
    record["environment"] = env_name
    record["langFlag"] = spec["lang"]
    return record


def main() -> int:
    parser = argparse.ArgumentParser()
    parser.add_argument("--binary", required=True)
    parser.add_argument("--out", type=pathlib.Path, default=None)
    parser.add_argument("--port", type=int, default=8899)
    parser.add_argument("--budget", type=int, default=8000)
    parser.add_argument("--headed", action="store_true",
                        help="run with a display (needed for WebGL, not for this)")
    parser.add_argument("--envs", default=",".join(ENVIRONMENTS),
                        help="comma-separated keys of ENVIRONMENTS")
    args = parser.parse_args()

    wanted = [name.strip() for name in args.envs.split(",") if name.strip()]
    unknown = [name for name in wanted if name not in ENVIRONMENTS]
    if unknown:
        parser.error("unknown environment(s): %s" % ",".join(unknown))

    server = serve(args.port)
    try:
        records = []
        for name in wanted:
            record = run_one(args.binary, name, ENVIRONMENTS[name], args.port,
                             args.budget, args.headed)
            status = "ok" if record else "NO DATA"
            print(f"  [{status}] {name}", flush=True)
            if record:
                records.append(record)
    finally:
        server.shutdown()

    if args.out:
        args.out.write_text(
            json.dumps(records, indent=2, ensure_ascii=False) + "\n",
            encoding="utf-8")
        print("wrote", args.out)

    print()
    header = f"{'env':8s} {'--lang':10s} {'language':10s} {'languages':32s} {'intl':10s} Accept-Language"
    print(header)
    print("-" * len(header))
    for record in records:
        print("%-8s %-10s %-10s %-32s %-10s %s" % (
            record.get("environment", "?"),
            record.get("langFlag", "?"),
            record.get("language", "?"),
            json.dumps(record.get("languages"), ensure_ascii=False),
            record.get("intlLocale", "?"),
            record.get("acceptLanguage", "?"),
        ))
    return 0 if records else 1


if __name__ == "__main__":
    raise SystemExit(main())
