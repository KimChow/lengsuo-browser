"""Apply the L3 wiring to a Chromium checkout (task K, extended by task O).

This used to carry every edit as an exact-string replacement, which meant the
wiring existed twice: here and in patches/0015 + patches/0016. The copies
drifted as soon as task O moved the document off the command line, and this
script kept installing the retired transport. It now replays the patches
themselves, so the patch files are the only source of truth.

Usage:
    python tools/rebuild/apply_l3_wiring.py [chromium-checkout]

The checkout defaults to the current directory. Pair with
tools/rebuild/replay_overlay_patches.sh for a from-scratch rebuild.
"""

import pathlib
import subprocess
import sys

ROOT = pathlib.Path(__file__).resolve().parents[2]
PATCHES = (
    "patches/0015-l3-storage.patch",
    "patches/0016-browser-wiring.patch",
)


def main(argv):
    checkout = pathlib.Path(argv[1] if len(argv) > 1 else ".").absolute()
    for rel in PATCHES:
        with open(ROOT / rel, "rb") as patch_file:
            done = subprocess.run(
                ["patch", "-p1", "-N", "--forward", "--fuzz=0", "-d",
                 str(checkout)],
                stdin=patch_file)
        if done.returncode != 0:
            print(f"FAILED: {rel}", file=sys.stderr)
            return 1
        print(f"applied {rel}")
    return 0


if __name__ == "__main__":
    raise SystemExit(main(sys.argv))
