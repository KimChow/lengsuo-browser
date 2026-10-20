"""Re-lock baseline.json to the exact bytes a clean rebuild would replay.

The digest has to be computed over the bytes *in the repository*, not over the
bytes in the working tree. On Windows the working tree serves CRLF for text
files while the repository stores LF, so hashing the working tree produced a
lock that the Linux build host could never satisfy: patch_apply rejected
every patch with "digest differs from baseline" even though nothing was
wrong with the patch.

The fix is to hash the blob git actually has staged, which is what a fresh
`git clone` gives every host:

    git cat-file blob :<path>        -> the staged content
    git cat-file blob HEAD:<path>    -> the committed content

The staged blob is preferred so that a not-yet-committed edit is still
lockable; the committed blob is the fallback for a path that is not staged.
"""

from __future__ import annotations

import hashlib
import json
import pathlib
import subprocess
import sys

ROOT = pathlib.Path(__file__).resolve().parents[2]


def _git(args: list[str]) -> bytes:
    return subprocess.run(
        ["git", *args], cwd=ROOT, capture_output=True, check=True).stdout


def blob_bytes(rel: str) -> bytes | None:
    """Return the repository's content for `rel`, or None when unknown."""
    for spec in (f":{rel}", f"HEAD:{rel}"):
        try:
            return _git(["cat-file", "blob", spec])
        except subprocess.CalledProcessError:
            continue
    return None


def main() -> int:
    lock_path = ROOT / "baseline.json"
    lock = json.loads(lock_path.read_text(encoding="utf-8"))
    locked = {entry["path"] for entry in lock["patches"]}

    for patch in sorted((ROOT / "patches").glob("*.patch")):
        rel = patch.relative_to(ROOT).as_posix()
        if rel not in locked:
            lock["patches"].append({"path": rel, "sha256": ""})
            locked.add(rel)
            print("added", rel)

    fallback = 0
    for entry in lock["patches"]:
        path = ROOT / entry["path"]
        if not path.exists():
            continue
        data = blob_bytes(entry["path"])
        if data is None:
            # Not in git at all: hash the working tree and say so, because
            # that digest will not match a clean clone either.
            data = path.read_bytes()
            fallback += 1
        entry["sha256"] = hashlib.sha256(data).hexdigest()

    lock_path.write_bytes(
        (json.dumps(lock, indent=2, ensure_ascii=False) + "\n").encode("utf-8"))
    print("locked", len(lock["patches"]), "patches")
    if fallback:
        print(f"warning: {fallback} patch(es) are not in git; their digest "
              f"was taken from the working tree", file=sys.stderr)
    return 0


if __name__ == "__main__":
    raise SystemExit(main())
