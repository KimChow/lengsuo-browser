"""Capture the fingerprint probe over an existing CDP endpoint.

``fp_probe.py`` launches the browser through Playwright, which adds automation
switches; ``navigator.webdriver`` then reflects the *harness*, not the browser.
This variant attaches to a browser the caller started normally, so the reading
matches what an ordinary visitor sees.

Usage::

    python tools/fp_probe_cdp.py --port 9333 --label ours-default --out out.json
"""

from __future__ import annotations

import argparse
import json
import pathlib
import sys

from playwright.sync_api import sync_playwright

PROBE = pathlib.Path(__file__).resolve().parents[1] / "tests" / "fp-probe.html"


def main(argv: list[str]) -> int:
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("--port", type=int, required=True)
    parser.add_argument("--label", required=True)
    parser.add_argument("--out", required=True)
    parser.add_argument("--timeout", type=int, default=45000)
    args = parser.parse_args(argv)

    with sync_playwright() as pw:
        browser = pw.chromium.connect_over_cdp(
            "http://127.0.0.1:%d" % args.port)
        try:
            context = browser.contexts[0] if browser.contexts \
                else browser.new_context()
            page = context.new_page()
            page.goto(PROBE.as_uri(), wait_until="load",
                      timeout=args.timeout)
            value = page.evaluate("() => window.__PROBE__")
            page.close()
        finally:
            browser.close()

    payload = {
        "label": args.label,
        "transport": "cdp-attach",
        "probe_path": str(PROBE),
        "value": value,
    }
    out = pathlib.Path(args.out)
    out.parent.mkdir(parents=True, exist_ok=True)
    out.write_text(
        json.dumps(payload, indent=2, ensure_ascii=False, sort_keys=True) + "\n",
        encoding="utf-8",
    )
    print("wrote %s (%s)" % (out, args.label))
    return 0


if __name__ == "__main__":
    raise SystemExit(main(sys.argv[1:]))
