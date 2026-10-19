"""Diff third-party verdicts between two runs, row by row.

Reads the JSON written by tools/online_scan.py and compares the *site's own*
CSS result classes (``result passed`` / ``failed`` / ``warn``). We never map a
class to "safe": we only report where our build differs from stock Chrome on
the same lab machine. Identical rows mean "no regression introduced by us",
which is the strongest claim this environment can support.

Usage:
  python3 tools/online_compare.py --stock /tmp/lab-stock --ours /tmp/lab-ours

Exit code 0 only when every compared row matches; 1 when a difference exists
(so it can gate CI). Differences are printed, never hidden.
"""
from __future__ import annotations

import argparse
import json
import pathlib
import sys


def verdict_rows(record: dict) -> dict[str, str]:
    """Flatten the site's tables into {row label: css class} pairs."""
    rows: dict[str, str] = {}
    for table in record.get("tables") or []:
        for row in table:
            if len(row) < 2:
                continue
            label = (row[0].get("text") or "").replace("\n", " ").strip()
            verdict = (row[1].get("text") or "").replace("\n", " ").strip()
            css = row[1].get("class") or ""
            if not label:
                continue
            rows[f"{label} => {verdict[:60]}"] = css
    return rows


def load(outdir: pathlib.Path) -> dict[str, dict]:
    """Map site name -> record, from <label>--<site>.json files."""
    found: dict[str, dict] = {}
    for path in sorted(outdir.glob("*--*.json")):
        record = json.loads(path.read_text(encoding="utf-8"))
        site = record.get("site")
        if site:
            found[site] = record
    return found


def main() -> int:
    parser = argparse.ArgumentParser()
    parser.add_argument("--stock", required=True)
    parser.add_argument("--ours", required=True)
    parser.add_argument("--strict", action="store_true",
                        help="also fail when a site is missing on one side")
    args = parser.parse_args()

    stock = load(pathlib.Path(args.stock))
    ours = load(pathlib.Path(args.ours))

    total = same = differ = 0
    problems: list[str] = []

    for site in sorted(set(stock) | set(ours)):
        if site not in stock or site not in ours:
            missing = "stock" if site not in stock else "ours"
            message = f"[{site}] missing in {missing}"
            print(message)
            if args.strict:
                problems.append(message)
            continue
        left = verdict_rows(stock[site])
        right = verdict_rows(ours[site])
        print(f"\n== {site} ==")
        print(f"   stock UA: {(stock[site].get('observed') or {}).get('ua', '?')}")
        print(f"   ours  UA: {(ours[site].get('observed') or {}).get('ua', '?')}")
        for label in sorted(set(left) | set(right)):
            total += 1
            a, b = left.get(label, "<absent>"), right.get(label, "<absent>")
            if a == b:
                same += 1
                continue
            differ += 1
            line = f"   DIFF {label}\n        stock={a!r} ours={b!r}"
            print(line)
            problems.append(f"{site}: {label} stock={a!r} ours={b!r}")

    print(f"\nrows compared={total} identical={same} different={differ}")
    if problems:
        print(f"RESULT: {len(problems)} difference(s) — review each above")
        return 1
    print("RESULT: identical to stock Chrome on every compared row")
    return 0


if __name__ == "__main__":
    sys.exit(main())
