"""Re-lock baseline.json to the exact bytes currently on disk.

Windows checkouts can serve CRLF where the repository stores LF, so the digest
must be computed from the bytes a clean rebuild would replay. Run the tests
afterwards: test_patch_replay verifies lock == disk.
"""

import hashlib
import json
import pathlib

ROOT = pathlib.Path(__file__).resolve().parents[2]


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

    for entry in lock["patches"]:
        path = ROOT / entry["path"]
        if path.exists():
            entry["sha256"] = hashlib.sha256(path.read_bytes()).hexdigest()

    lock_path.write_bytes(
        (json.dumps(lock, indent=2, ensure_ascii=False) + "\n").encode("utf-8"))
    print("locked", len(lock["patches"]), "patches")
    return 0


if __name__ == "__main__":
    raise SystemExit(main())
