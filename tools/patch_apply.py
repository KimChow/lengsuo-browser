"""Plan/apply reviewed baseline patches; never attests a build or browser."""

import argparse
import hashlib
import json
import os
from pathlib import Path
import subprocess

from tools.baseline import BaselineError, _git, inspect_checkout, read_lock
from tools.overlay import MAX_SOURCE

PATCH_TIMEOUT = 45


class PatchApplyError(ValueError):
    """Unsafe or inconsistent patch application."""


def _declared_names(lock):
    entries = lock["patches"]
    if not entries:
        raise PatchApplyError("Baseline declares no patches")
    return sorted(Path(entry["path"]).name for entry in entries)


def _actual_names(repository):
    directory = Path(repository) / "patches"
    if not directory.is_dir():
        return []
    return sorted(item.name for item in directory.iterdir()
                  if item.is_file() and item.name.endswith(".patch"))


def _read_patch(repository, name):
    path = Path(repository) / "patches" / name
    data = path.read_bytes()
    if len(data) > MAX_SOURCE:
        raise PatchApplyError(f"Patch exceeds size limit: {name}")
    return data


def _apply_stream(source, data, reverse=False, check_only=False):
    args = ["git", "-C", str(source), "apply"]
    if check_only:
        args.append("--check")
    if reverse:
        args.append("--reverse")
    args.append("-")
    env = {**os.environ, "GIT_TERMINAL_PROMPT": "0", "GIT_NO_LAZY_FETCH": "1"}
    try:
        result = subprocess.run(args, input=data, capture_output=True,
                                env=env, timeout=PATCH_TIMEOUT)
    except (OSError, subprocess.SubprocessError) as error:
        raise PatchApplyError("Git apply invocation failed") from error
    return result.returncode


def execute(source, repository, lock, apply=False):
    receipt = {"schema_version": 1, "mode": "apply" if apply else "plan",
               "status": "failed", "patches": [],
               "build_verified": False, "browser_integrated": False,
               "upstream_tree_modified": False}
    try:
        source = Path(source).absolute()
        repository = Path(repository).absolute()
        declared = _declared_names(lock)
        if declared != _actual_names(repository):
            raise PatchApplyError(
                "Baseline patch list does not match patches/ directory")
        for entry in lock["patches"]:
            name = Path(entry["path"]).name
            digest = hashlib.sha256(_read_patch(repository, name)).hexdigest()
            if digest != entry["sha256"]:
                raise PatchApplyError(
                    f"Patch digest differs from baseline: {name}")
        receipt["baseline"] = inspect_checkout(source, lock)
        if Path(_git(source, "rev-parse", "--show-toplevel").decode().strip()) != source:
            raise PatchApplyError("Source must be the checkout root")
        # Source placement is tools.overlay's responsibility; this tool only
        # applies the reviewed baseline patch sequence to a clean tree.
        if all(_apply_stream(source, _read_patch(repository, name),
                             reverse=True, check_only=True) == 0
               for name in declared):
            receipt["patches"] = [{"path": f"patches/{name}",
                                   "action": "already-applied"}
                                  for name in declared]
            receipt["status"] = "applied"
            receipt["upstream_tree_modified"] = True
            return receipt
        if (_git(source, "diff", "HEAD", "--name-only").decode().strip()
                or _git(source, "diff", "--cached", "--name-only").decode().strip()):
            raise PatchApplyError(
                "Source tree must be clean of tracked changes before applying patches")
        for name in declared:
            if _apply_stream(source, _read_patch(repository, name),
                             check_only=True) != 0:
                raise PatchApplyError(f"Patch does not apply cleanly: {name}")
        if apply:
            for name in declared:
                if _apply_stream(source, _read_patch(repository, name)) != 0:
                    raise PatchApplyError(f"Patch application failed: {name}")
                receipt["patches"].append({"path": f"patches/{name}",
                                           "action": "applied"})
            receipt["upstream_tree_modified"] = True
        else:
            receipt["patches"] = [{"path": f"patches/{name}",
                                   "action": "check-passed"}
                                  for name in declared]
        receipt["status"] = "applied" if apply else "planned"
    except (PatchApplyError, BaselineError, OSError, ValueError,
            UnicodeError) as error:
        receipt["error"] = str(error)
    return receipt


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("--source", type=Path, required=True)
    parser.add_argument("--repository", type=Path, required=True)
    parser.add_argument("--lock", type=Path, required=True)
    parser.add_argument("--apply", action="store_true")
    args = parser.parse_args()
    try:
        receipt = execute(args.source, args.repository,
                          read_lock(args.lock), args.apply)
    except (BaselineError, OSError, ValueError) as error:
        receipt = {"status": "failed", "error": str(error), "patches": [],
                   "build_verified": False, "browser_integrated": False}
    print(json.dumps(receipt, indent=2))
    return 1 if receipt["status"] == "failed" else 0


if __name__ == "__main__":
    raise SystemExit(main())
