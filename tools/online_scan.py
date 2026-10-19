"""Collect third-party reports, never assign a detector verdict ourselves.

Linux headed browser + Xvfb is a lab environment, not final desktop acceptance.
CDP attaches to an explicitly selected executable with a fresh owned profile.
No JS property overrides, stealth flags, fake responses, or TLS-error bypasses.
Screenshots and innerText preserve the external site's result. A successful
capture does NOT mean the browser passed. Compare versions/environment first.
Requires Playwright installed in an isolated environment and xvfb-run on Linux.
"""
from __future__ import annotations

import argparse
import json
import os
from pathlib import Path
import signal
import socket
import subprocess
import tempfile
import time
from urllib.request import urlopen

SITES = {
    "creepjs": "https://abrahamjuliot.github.io/creepjs/",
    "sannysoft": "https://bot.sannysoft.com/",
    "browserleaks-js": "https://browserleaks.com/javascript",
    "browserleaks-canvas": "https://browserleaks.com/canvas",
    "browserleaks-webgl": "https://browserleaks.com/webgl",
    "pixelscan": "https://pixelscan.net/",
    "browserscan": "https://www.browserscan.net/",
    "iphey": "https://iphey.com/",
    "incolumitas": "https://bot.incolumitas.com/",
}


def free_port():
    with socket.socket() as sock:
        sock.bind(("127.0.0.1", 0))
        return sock.getsockname()[1]


def capture(playwright, args, name, root):
    port = free_port()
    url = SITES[name]
    # Only the temporary profile created here is removed by TemporaryDirectory.
    with tempfile.TemporaryDirectory(prefix="lengsuo-online-", ignore_cleanup_errors=True) as profile:
        flags = ["--no-first-run", "--no-default-browser-check",
                 f"--user-data-dir={profile}",
                 f"--remote-debugging-port={port}",
                 "--remote-debugging-address=127.0.0.1"]
        # Lab process has no system sandbox; do not use this flag on desktop.
        if args.no_sandbox:
            flags.append("--no-sandbox")
        if args.fp:
            flags.append("--lengsuo-managed-fp=" + args.fp)
        command = ["xvfb-run", "-a", "--server-args=-screen 0 1440x1000x24",
                   args.binary, *flags, "about:blank"]
        record = {"site": name, "url": url, "binary": args.binary,
                  "flags": [x for x in flags if "user-data-dir" not in x],
                  "capture_status": "not_started", "detector_verdict": "unreviewed",
                  "environment": "Linux Xvfb headed; CDP attached; virtual display"}
        stem = root / (args.label + "--" + name)
        with open(str(stem) + ".stderr.log", "w", encoding="utf-8") as log:
            proc = subprocess.Popen(command, stdout=subprocess.DEVNULL,
                                    stderr=log, start_new_session=True)
            browser = None
            try:
                deadline = time.monotonic() + 30
                while True:
                    if proc.poll() is not None:
                        raise RuntimeError("selected browser exited before CDP ready")
                    try:
                        with urlopen(f"http://127.0.0.1:{port}/json/version",
                                     timeout=2) as response:
                            record["cdp_version"] = json.load(response)
                        break
                    except OSError:
                        if time.monotonic() >= deadline:
                            raise TimeoutError("CDP startup timed out")
                        time.sleep(0.25)
                browser = playwright.chromium.connect_over_cdp(
                    f"http://127.0.0.1:{port}", timeout=15000)
                page = browser.contexts[0].pages[0]
                page.set_default_timeout(15000)
                # Read actual capabilities for context; do not change any value.
                record["observed"] = page.evaluate("""() => ({
                    ua: navigator.userAgent, webdriver: navigator.webdriver,
                    language: navigator.language,
                    timezone: Intl.DateTimeFormat().resolvedOptions().timeZone,
                    cores: navigator.hardwareConcurrency,
                    screen: [screen.width, screen.height, devicePixelRatio]
                })""")
                try:
                    response = page.goto(url, wait_until="domcontentloaded",
                                         timeout=60000)
                    record["http_status"] = response.status if response else None
                except Exception as exc:
                    record["navigation_error"] = str(exc)
                # Start the site's own scanner using its normal public button.
                # Do not solve behavior challenges or bypass CAPTCHAs.
                if name == "pixelscan":
                    start = page.get_by_text("Scan My Browser Now", exact=True)
                    try:
                        start.first.click(timeout=15000)
                        record["site_action"] = "clicked public Scan My Browser Now"
                    except Exception as exc:
                        record["site_action_error"] = str(exc)
                # Real elapsed wait; virtual-time budgets change API behavior.
                page.wait_for_timeout(args.wait * 1000)
                record["final_url"] = page.url
                record["title"] = page.title()
                text = page.locator("body").inner_text(timeout=15000)
                Path(str(stem) + ".txt").write_text(text, encoding="utf-8")
                # Full-page screenshot: no clipping; the text is a second record.
                page.screenshot(path=str(stem) + ".png", full_page=True,
                                timeout=30000)
                record["capture_status"] = "captured_not_passed"
                record["text_chars"] = len(text)
                # Preserve site-rendered result cells, including its CSS classes.
                record["tables"] = page.locator("table").evaluate_all("""tables =>
                    tables.map(t => Array.from(t.rows).map(r =>
                      Array.from(r.cells).map(c => ({
                        text: c.innerText, class: c.className
                      }))))""")
            except Exception as exc:
                record["capture_status"] = "error"
                record["error"] = str(exc)
            finally:
                if browser:
                    try:
                        browser.close()
                    except Exception:
                        pass
                # Only our newly created browser/Xvfb process group is stopped.
                try:
                    os.killpg(proc.pid, signal.SIGTERM)
                except ProcessLookupError:
                    pass
                try:
                    proc.wait(timeout=10)
                except subprocess.TimeoutExpired:
                    os.killpg(proc.pid, signal.SIGKILL)
                    proc.wait()
        Path(str(stem) + ".json").write_text(
            json.dumps(record, ensure_ascii=False, indent=2), encoding="utf-8")
        return record


def main():
    from playwright.sync_api import sync_playwright

    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("--binary", required=True)
    parser.add_argument("--label", required=True)
    parser.add_argument("--out", required=True)
    parser.add_argument("--sites", default="sannysoft,creepjs,browserleaks-webgl")
    parser.add_argument("--wait", type=int, default=30)
    parser.add_argument("--no-sandbox", action="store_true")
    parser.add_argument("--fp", help="Optional explicit test payload, not a production profile")
    args = parser.parse_args()
    if not args.label.replace("-", "").isalnum():
        parser.error("label must be alphanumeric or hyphen")
    names = args.sites.split(",")
    if any(name not in SITES for name in names):
        parser.error("unknown site")
    root = Path(args.out).resolve()
    root.mkdir(parents=True, exist_ok=True)
    with sync_playwright() as playwright:
        records = []
        for name in names:
            record = capture(playwright, args, name, root)
            records.append(record)
            print(args.label, name, record["capture_status"],
                  record.get("error", ""), flush=True)
    (root / (args.label + "-index.json")).write_text(
        json.dumps(records, ensure_ascii=False, indent=2), encoding="utf-8")


if __name__ == "__main__":
    main()
