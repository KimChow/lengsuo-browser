"""Compare two lab runs on the rows that reveal real fingerprint differences.

Ignores run-to-run noise (clock, viewport size, network downlink) and reports
only rows whose value should be identical between stock Chrome and our build,
plus the site's own verdict classes so nothing is hidden.
"""
import json
import pathlib
import sys

NOISY = ("datetimeformat", "system time", "tolocalestring", "screen resolution",
         "div.clientheight", "window.innerheight", "downlink", "rtt",
         "effective type", "connection", "speech voices", "cpuperformance")


def rows(path):
    record = json.loads(path.read_text())
    out = {}
    for table in record.get("tables") or []:
        for row in table:
            if len(row) < 2:
                continue
            label = (row[0].get("text") or "").replace("\n", " ").strip()
            value = (row[1].get("text") or "").replace("\n", " ").strip()
            if label:
                out[label] = (value, row[1].get("class") or "")
    return record, out


def main():
    stock_dir = pathlib.Path(sys.argv[1])
    ours_dir = pathlib.Path(sys.argv[2])
    sites = sorted({p.name.split("--")[1].rsplit(".", 1)[0]
                    for p in list(stock_dir.glob("*--*.json")) +
                    list(ours_dir.glob("*--*.json"))})
    for site in sites:
        stock_files = list(stock_dir.glob("*--%s.json" % site))
        ours_files = list(ours_dir.glob("*--%s.json" % site))
        if not stock_files or not ours_files:
            print("[%s] missing on one side" % site)
            continue
        srec, srows = rows(stock_files[0])
        orec, orows = rows(ours_files[0])
        print("\n== %s ==" % site)
        print("  stock UA:", (srec.get("observed") or {}).get("ua"))
        print("  ours  UA:", (orec.get("observed") or {}).get("ua"))
        shown = 0
        for label in sorted(set(srows) & set(orows)):
            if any(token in label.lower() for token in NOISY):
                continue
            sv, sc = srows[label]
            ov, oc = orows[label]
            if sv == ov and sc == oc:
                continue
            print("  DIFF %-34s stock=%r[%s] ours=%r[%s]"
                  % (label[:34], sv[:46], sc, ov[:46], oc))
            shown += 1
        if not shown:
            print("  no meaningful difference")
        # Verdict tallies come straight from the site's CSS classes.
        for name, rec in (("stock", srec), ("ours", orec)):
            passed = failed = warn = 0
            for table in rec.get("tables") or []:
                for row in table:
                    if len(row) < 2:
                        continue
                    css = row[1].get("class") or ""
                    if "passed" in css:
                        passed += 1
                    elif "failed" in css:
                        failed += 1
                    elif "warn" in css:
                        warn += 1
            print("  %-5s site classes: passed=%d failed=%d warn=%d"
                  % (name, passed, failed, warn))


if __name__ == "__main__":
    main()
