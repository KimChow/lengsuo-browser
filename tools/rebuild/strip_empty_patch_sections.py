"""Strip leftover file sections from the patch set.

A section that carries a `---` line but no `+++` line and no hunk is a
leftover from when another patch took the file over. `patch` reads it as a
malformed section and refuses the whole file, so the series cannot be
replayed. Two of these were found the hard way:

  * patches/0008-fp-overrides.patch  left a section for
    chrome_content_browser_client.cc behind after 0016 took the file;
  * patches/0007-canvas-readback.patch left one for renderer_main.cc.

Run this after moving a file to a different patch, then re-lock the
baseline. The test suite refuses a patch that still has such a section.
"""

from __future__ import annotations

import pathlib
import sys

ROOT = pathlib.Path(__file__).resolve().parents[2]
PATCHES = ROOT / "patches"


def strip(path: pathlib.Path) -> list[str]:
    text = path.read_text(encoding="utf-8", errors="surrogateescape")
    lines = text.splitlines(keepends=True)
    out: list[str] = []
    removed: list[str] = []
    i = 0
    while i < len(lines):
        if lines[i].startswith("diff --git "):
            j = i + 1
            while j < len(lines) and not lines[j].startswith("diff --git "):
                j += 1
            section = lines[i:j]
            has_hunk = any(l.startswith("@@") for l in section)
            has_new = any(l.startswith("+++ ") for l in section)
            if not has_hunk and not has_new:
                removed.append(lines[i].strip())
                i = j
                continue
            out.extend(section)
            i = j
        else:
            out.append(lines[i])
            i += 1
    if removed:
        path.write_text("".join(out), encoding="utf-8", errors="surrogateescape")
    return removed


def main() -> int:
    total = 0
    for patch in sorted(PATCHES.glob("*.patch")):
        removed = strip(patch)
        if removed:
            total += len(removed)
            print(patch.name, "removed", len(removed), "empty section(s)")
            for header in removed:
                print("   ", header[:110])
    if not total:
        print("no empty sections found")
    return 0


if __name__ == "__main__":
    sys.exit(main())
