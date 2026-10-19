"""Capture the fingerprint probe from any Chromium-family binary.

The probe itself lives in ``tests/fp-probe.html`` and runs as ordinary page
script; nothing is injected through CDP. Whatever the page reports is therefore
what a site would see, which keeps this a measurement tool rather than a
self-graded assertion suite.

Usage (under a virtual display on a headless host)::

    xvfb-run -a python tools/fp_probe.py \
        --binary /path/to/chrome --label ours-default --out out.json

    xvfb-run -a python tools/fp_probe.py \
        --binary /path/to/chrome --label ours-profile --out out.json \
        --fp 'seed:0:0:0:0:1:0:1:Vendor:Renderer:0:0'
"""

from __future__ import annotations

import argparse
import json
import pathlib
import sys

from playwright.sync_api import sync_playwright

PROBE = pathlib.Path(__file__).resolve().parents[1] / "tests" / "fp-probe.html"


def browser_args(sandbox: bool, fp: str | None) -> list[str]:
    """Chromium flags for the measurement run.

    Only sandbox/switch flags: no UA override, no feature nudging, no stealth
    arguments. Faking the environment here would invalidate the measurement.
    """
    args: list[str] = []
    if not sandbox:
        args.extend(["--no-sandbox", "--disable-dev-shm-usage"])
    if fp:
        args.append("--lengsuo-managed-fp=" + fp)
    return args


def capture(binary: str, fp: str | None, sandbox: bool,
            timeout_ms: int) -> dict:
    url = PROBE.as_uri()
    with sync_playwright() as pw:
        browser = pw.chromium.launch(
            executable_path=binary,
            headless=False,
            args=browser_args(sandbox, fp),
        )
        try:
            context = browser.new_context()
            page = context.new_page()
            page.goto(url, wait_until="load", timeout=timeout_ms)
            value = page.evaluate("() => window.__PROBE__")
            context.close()
            if not isinstance(value, dict):
                raise RuntimeError("probe returned %r" % type(value))
            return value
        finally:
            browser.close()


def main(argv: list[str]) -> int:
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("--binary", required=True)
    parser.add_argument("--label", required=True)
    parser.add_argument("--out", required=True)
    parser.add_argument("--fp", default=None,
                        help="value for --lengsuo-managed-fp")
    parser.add_argument("--sandbox", action="store_true",
                        help="keep the Chromium sandbox (needs privileges)")
    parser.add_argument("--timeout", type=int, default=45000)
    args = parser.parse_args(argv)

    try:
        data = capture(args.binary, args.fp, args.sandbox, args.timeout)
    except Exception as exc:  # noqa: BLE001 - surfaced verbatim to the caller
        print("probe failed: %s" % exc, file=sys.stderr)
        return 1

    payload = {
        "label": args.label,
        "binary": args.binary,
        "probe_path": str(PROBE),
        "fp_switch": args.fp,
        "value": data,
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
