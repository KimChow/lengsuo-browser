"""Apply a patch set hunk by hunk, skipping hunks that are already in place.

The patches in this repository were not all captured from a clean baseline:
several were frozen from a tree where earlier patches were already applied, so
they carry those earlier changes a second time. Applying them in order with
``git apply`` therefore fails, and ``patch --forward`` skips whole patches on
the first sign of an already-applied hunk — which silently drops later hunks
(a 12-field switch once went missing that way).

This tool works at hunk granularity:

  * a hunk whose added lines are already present is reported as ``dup`` and
    skipped;
  * a hunk that does not apply and is not already present is ``failed`` and
    makes the run fail, so nothing is lost silently.

Usage::

    python3 apply_hunks.py --source /path/to/chromium/src --patches /path/to/patches
"""

from __future__ import annotations

import argparse
import pathlib
import subprocess
import sys

HEADER_KEYS = ("--- ", "+++ ", "diff --git ", "index ", "new file mode",
               "deleted file mode", "similarity index", "old mode", "new mode")


def split_patch(text: str) -> list[dict]:
    """Return [{header: [...], body: [...], files: (old, new)}] per hunk."""
    lines = text.splitlines(keepends=True)
    sections: list[dict] = []
    header: list[str] = []
    pending: dict | None = None

    for line in lines:
        if line.startswith("@@"):
            if pending is not None:
                sections.append(pending)
            pending = {"header": list(header), "body": [line], "files": None}
            continue
        if pending is None:
            header.append(line)
            continue
        if line.startswith(HEADER_KEYS):
            sections.append(pending)
            header = [line]
            pending = None
            continue
        pending["body"].append(line)

    if pending is not None:
        sections.append(pending)

    for section in sections:
        for line in section["header"]:
            if line.startswith("+++ "):
                path = line[4:].strip().split("\t")[0]
                # git diff writes b/<path>; strip the side prefix.
                if path.startswith("b/"):
                    path = path[2:]
                section["files"] = path
    return sections


def added_lines(body: list[str]) -> list[str]:
    return [line[1:] for line in body if line.startswith("+") and
            not line.startswith("+++")]


def main(argv: list[str]) -> int:
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("--source", required=True)
    parser.add_argument("--patches", required=True)
    parser.add_argument("--only", default=None,
                        help="apply a single patch file by name")
    args = parser.parse_args(argv)

    source = pathlib.Path(args.source)
    patches = sorted(pathlib.Path(args.patches).glob("*.patch"))
    if args.only:
        patches = [p for p in patches if p.name == args.only]

    totals = {"applied": 0, "dup": 0, "failed": 0}
    for patch in patches:
        sections = split_patch(patch.read_text(encoding="utf-8", errors="surrogateescape"))
        stats = {"applied": 0, "dup": 0, "failed": 0}
        for index, section in enumerate(sections):
            target = source / section["files"] if section["files"] else None
            wants = added_lines(section["body"])
            is_new = any("new file mode" in line for line in section["header"])
            if target is None or (not target.exists() and not is_new):
                stats["failed"] += 1
                print("%s hunk#%d missing target %s" % (patch.name, index, section["files"]))
                continue
            if target is not None and target.exists():
                current = target.read_text(encoding="utf-8", errors="surrogateescape")
                if wants and all(line in current for line in wants):
                    stats["dup"] += 1
                    continue
            temp = source / ".hunk.patch"
            temp.write_text("".join(section["header"]) + "".join(section["body"]),
                            encoding="utf-8", errors="surrogateescape")
            result = subprocess.run(
                ["git", "apply", "--whitespace=nowarn", str(temp)],
                cwd=source, capture_output=True, text=True)
            temp.unlink(missing_ok=True)
            if result.returncode == 0:
                stats["applied"] += 1
            else:
                stats["failed"] += 1
                print("%s hunk#%d failed: %s" % (
                    patch.name, index,
                    (result.stderr or "").strip().splitlines()[:1]))
        print("%-40s applied=%d dup=%d failed=%d"
              % (patch.name, stats["applied"], stats["dup"], stats["failed"]))
        for key in totals:
            totals[key] += stats[key]

    print("TOTAL applied=%d dup=%d failed=%d"
          % (totals["applied"], totals["dup"], totals["failed"]))
    return 1 if totals["failed"] else 0


if __name__ == "__main__":
    raise SystemExit(main(sys.argv[1:]))
