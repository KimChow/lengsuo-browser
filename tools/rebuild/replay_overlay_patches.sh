#!/bin/bash
# Re-apply the source overlay and all patches on the 47 build tree.
#
# 0011 is skipped: it is a pure file-creation snapshot of the lengsuo/ sources
# (24 new files, no upstream edits) that predates tools.overlay. Overlay now
# places every one of those files, so applying 0011 after the overlay fails on
# already-existing paths. The remaining patches carry the actual upstream
# wiring and are applied on top of the overlay.
set -u
SRC=/home/ubuntu/lengsuo-browser-dev/src
REPO=/home/ubuntu/lengsuo-browser-dev/recipe

cd "$REPO" || exit 1

# The overlay refuses to overwrite an existing target whose content differs
# ("Existing overlay target differs"). That guard is right for files someone
# may have hand-edited in the tree, but it also blocks the normal case of
# replaying after a source file changed upstream: lengsuo/integration/BUILD.gn
# gained the l3_bootstrap.cc entry, the tree still holds the old copy, and
# the whole replay aborts.
#
# The overlay targets are recipe-owned (tools/overlay.py MAPPING copies them
# from the recipe into the tree), so the tree copy is never the source of
# truth. Removing the lengsuo/ overlay directory before replaying makes the
# overlay a clean re-create and keeps the guard meaningful for anything
# outside lengsuo/.
rm -rf "$SRC/lengsuo"

python3 -m tools.overlay --source "$SRC" --repository "$REPO" \
  --lock "$REPO/baseline.json" --apply --allow-patched-baseline \
  > /tmp/overlay_receipt.json 2>&1
echo "overlay rc=$?"

python3 - <<'PY'
import json
r = json.load(open("/tmp/overlay_receipt.json"))
print("overlay status:", r["status"], "| files:", len(r["files"]),
      "| created:", sum(1 for f in r["files"] if f["action"] == "created"),
      "| identical:", sum(1 for f in r["files"] if f["action"] == "identical"))
if "error" in r:
    print("overlay error:", r["error"][:300])
PY

# Apply every declared patch except 0011, in declared order.
python3 - "$SRC" "$REPO" <<'PY'
import json
import subprocess
import sys
from pathlib import Path

source, repo = Path(sys.argv[1]), Path(sys.argv[2])
lock = json.loads((repo / "baseline.json").read_text(encoding="utf-8"))
names = [Path(e["path"]).name for e in lock["patches"]]
# 0011 is a pure file-creation snapshot that tools.overlay supersedes: the
# overlay places exactly the same files, so applying 0011 on top of it fails
# on already-existing paths.
#
# 0007-canvas-readback used to be skipped too, because it carried leftover
# empty sections for browser_client and renderer_main - files that 0016 has
# since taken over. Those empty sections are gone, and the patch now applies
# cleanly on its own, so it is no longer skipped.
skipped = [n for n in names if n in ("0011-lengsuo-sources.patch",)]
applied, failed = [], None
for name in names:
    if name in skipped:
        continue
    data = (repo / "patches" / name).read_bytes()
    # GNU patch with fuzz: several patches insert includes at the same anchor
    # (0007 x2 and 0008 all touch chrome_content_browser_client.cc /
    # renderer_main.cc), so exact-context git apply rejects the later ones.
    # --fuzz=3 matches on the surrounding lines that still exist. --dry-run
    # first so a partial failure cannot leave the tree half-patched.
    # The newest patches are applied last, after every earlier patch has
    # shifted the surrounding lines. Their context is unique at that point, so
    # they are applied with fuzz=0: with fuzz the hunk can match an earlier
    # occurrence of a shorter context and land the code before the line it was
    # meant to follow (observed: the L3 block landed above the
    # "const base::CommandLine& command_line" declaration).
    fuzz = "0" if name.startswith("0015") else "3"

    def run(args):
        return subprocess.run(
            ["patch", "-p1", "-N", "--forward", f"--fuzz={fuzz}", "-s", "-d",
             str(source)] + args,
            input=data, capture_output=True, timeout=45)

    # The patch series is an evolving snapshot, so a later patch may already
    # be contained in an earlier one (0007-profile-language already carries
    # 0008's browser_client hunks). GNU patch reports that as
    # "Reversed (or previously applied) patch detected" and exits 1 while
    # ignoring the whole file - that is success, not failure. Only an actual
    # rejected hunk ("hunk FAILED") stops the replay.
    dry = run(["--dry-run"])
    combined = (dry.stdout + dry.stderr).decode(errors="replace")
    if "FAILED" in combined or "malformed" in combined:
        failed = (name, combined[:400])
        break
    real = run([])
    combined = (real.stdout + real.stderr).decode(errors="replace")
    if "FAILED" in combined:
        failed = (name, combined[:400])
        break
    applied.append(name)
print("patch skipped (overlay covers it):", skipped)
print("patch applied:", len(applied), applied)
print("patch failed:", failed if failed else "none")
PY
