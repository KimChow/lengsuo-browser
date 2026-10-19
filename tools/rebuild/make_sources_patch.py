"""Regenerate patches/0011-lengsuo-sources.patch from this repository.

The patch is what a clean rebuild replays, so it must be reproducible from the
repo alone — never captured from a build host's working tree. That capture
path has bitten twice:

  * ``git diff ... > patch 2>&1`` spliced a CRLF warning into the middle of a
    file's content, corrupting the payload;
  * the sources were copied to a second directory, so the patch and the
    overlay disagreed.

Generating in a throwaway git repository with ``-text`` attributes removes both
failure modes: no warning text can reach the output, and every byte comes from
the repository.

Usage::

    python tools/rebuild/make_sources_patch.py [--check]

``--check`` regenerates into memory and fails if the committed patch differs,
which is what the test suite uses.
"""

from __future__ import annotations

import argparse
import pathlib
import shutil
import subprocess
import sys
import tempfile

ROOT = pathlib.Path(__file__).resolve().parents[2]
sys.path.insert(0, str(ROOT))

from tools.overlay import MAPPING  # noqa: E402

PATCH = ROOT / "patches" / "0011-lengsuo-sources.patch"


def _git(args: list[str], cwd: pathlib.Path) -> str:
    result = subprocess.run(
        ["git", *args], cwd=cwd, capture_output=True, text=True, check=True)
    return result.stdout


def build_patch() -> str:
    with tempfile.TemporaryDirectory(prefix="lq-patch-") as tmp:
        work = pathlib.Path(tmp)
        _git(["init", "-q"], work)
        _git(["config", "core.autocrlf", "false"], work)
        _git(["config", "user.email", "patch@lengsuo.invalid"], work)
        _git(["config", "user.name", "lengsuo patch"], work)
        # Without this git warns about line endings on every file, and a warning
        # is exactly what corrupted the previous patch.
        (work / ".gitattributes").write_text("* -text\n", encoding="utf-8")

        for source_rel, target_rel in MAPPING:
            source = ROOT / source_rel
            if not source.exists():
                raise SystemExit("missing source %s" % source_rel)
            destination = work / target_rel
            destination.parent.mkdir(parents=True, exist_ok=True)
            shutil.copyfile(source, destination)

        _git(["add", "-A", "."], work)
        patch = _git(["diff", "--cached", "--binary", "--no-color"], work)

    # The .gitattributes file is scaffolding, not part of the payload.
    lines = patch.splitlines()
    kept: list[str] = []
    skip = False
    for line in lines:
        if line.startswith("diff --git "):
            skip = line.rstrip().endswith("b/.gitattributes")
        if not skip:
            kept.append(line)
    return "\n".join(kept) + "\n"


def main(argv: list[str]) -> int:
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("--check", action="store_true",
                        help="fail instead of writing when the patch is stale")
    args = parser.parse_args(argv)

    generated = build_patch()
    if args.check:
        current = PATCH.read_text(encoding="utf-8") if PATCH.exists() else ""
        if current != generated:
            print("patch 0011 is stale; run tools/rebuild/make_sources_patch.py",
                  file=sys.stderr)
            return 1
        print("patch 0011 matches the repository sources")
        return 0

    PATCH.write_bytes(generated.encode("utf-8"))
    print("wrote %s (%d lines)" % (PATCH, len(generated.splitlines())))
    return 0


if __name__ == "__main__":
    raise SystemExit(main(sys.argv[1:]))
