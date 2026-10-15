"""Verify pinned Git objects; this deliberately does not attest a browser build."""

import argparse
import hashlib
import json
import os
from pathlib import Path
import re
import subprocess

UPSTREAM = "https://chromium.googlesource.com/chromium/src.git"
FIELDS = {"schema_version", "upstream_url", "chromium_version", "chromium_revision",
          "deps_sha256", "target_platforms", "patches"}
MAX_METADATA = 1024 * 1024


class BaselineError(ValueError):
    """Invalid or mismatching baseline."""


def validate_lock(raw):
    if type(raw) is not dict or set(raw) != FIELDS:
        raise BaselineError("Unexpected baseline fields")
    if type(raw["schema_version"]) is not int or raw["schema_version"] != 1:
        raise BaselineError("Unsupported schema version")
    if raw["upstream_url"] != UPSTREAM:
        raise BaselineError("Unexpected upstream")
    for field, pattern in [
        ("chromium_version", r"(?:0|[1-9][0-9]*)\.(?:0|[1-9][0-9]*)\.(?:0|[1-9][0-9]*)\.(?:0|[1-9][0-9]*)"),
        ("chromium_revision", r"[0-9a-f]{40}"),
        ("deps_sha256", r"[0-9a-f]{64}"),
    ]:
        if not isinstance(raw[field], str) or not re.fullmatch(pattern, raw[field]):
            raise BaselineError(f"Invalid {field}")
    platforms = raw["target_platforms"]
    if (type(platforms) is not list or not platforms
            or any(type(p) is not str or p not in {"windows", "linux", "macos"} for p in platforms)
            or len(set(platforms)) != len(platforms)):
        raise BaselineError("Invalid target platforms")
    if type(raw["patches"]) is not list:
        raise BaselineError("Invalid patches")
    paths = set()
    for entry in raw["patches"]:
        if type(entry) is not dict or set(entry) != {"path", "sha256"}:
            raise BaselineError("Unexpected patch fields")
        name, digest = entry["path"], entry["sha256"]
        if (not isinstance(name, str) or not name.startswith("patches/")
                or not name.endswith(".patch") or name in paths
                or any(part in {"", ".", ".."} or not re.fullmatch(r"[A-Za-z0-9_.-]+", part)
                       for part in name.split("/"))):
            raise BaselineError("Unsafe or duplicate patch path")
        if not isinstance(digest, str) or not re.fullmatch(r"[0-9a-f]{64}", digest):
            raise BaselineError("Invalid patch digest")
        paths.add(name)
    return raw


def _unique_object(pairs):
    result = {}
    for key, value in pairs:
        if key in result:
            raise BaselineError("Duplicate JSON key")
        result[key] = value
    return result


def read_lock(path):
    try:
        with Path(path).open("rb") as stream:
            content = stream.read(MAX_METADATA + 1)
        if len(content) > MAX_METADATA:
            raise BaselineError("Baseline exceeds size limit")
        return validate_lock(json.loads(content.decode("utf-8"), object_pairs_hook=_unique_object))
    except (OSError, UnicodeError, json.JSONDecodeError) as error:
        raise BaselineError("Cannot read baseline JSON") from error


def _git(source, *args):
    env = {**os.environ, "GIT_TERMINAL_PROMPT": "0", "GIT_NO_LAZY_FETCH": "1"}
    try:
        return subprocess.run(
            ["git", "-C", str(source), *args], check=True, capture_output=True,
            timeout=45, env=env,
        ).stdout
    except (OSError, subprocess.SubprocessError) as error:
        raise BaselineError("Git baseline inspection failed") from error


def _blob(source, revision, name):
    obj = f"{revision}:{name}"
    size = int(_git(source, "cat-file", "-s", obj))
    if size > MAX_METADATA:
        raise BaselineError("Source metadata exceeds size limit")
    return _git(source, "show", obj)


def inspect_checkout(source, lock):
    validate_lock(lock)
    if _git(source, "remote", "get-url", "origin").decode().strip() != lock["upstream_url"]:
        raise BaselineError("Checkout origin differs from baseline")
    revision = _git(source, "rev-parse", "HEAD").decode().strip()
    if revision != lock["chromium_revision"]:
        raise BaselineError("Checkout HEAD differs from baseline")
    tagged = _git(source, "rev-parse", f"refs/tags/{lock['chromium_version']}^{{commit}}").decode().strip()
    if tagged != revision:
        raise BaselineError("Release tag differs from revision")
    version_lines = _blob(source, revision, "chrome/VERSION").decode("ascii").splitlines()
    parts = dict(line.split("=", 1) for line in version_lines if "=" in line)
    actual_version = ".".join(parts.get(key, "") for key in ("MAJOR", "MINOR", "BUILD", "PATCH"))
    if actual_version != lock["chromium_version"]:
        raise BaselineError("Source version differs from release tag")
    deps_digest = hashlib.sha256(_blob(source, revision, "DEPS")).hexdigest()
    if deps_digest != lock["deps_sha256"]:
        raise BaselineError("Source DEPS differs from baseline")
    return {"chromium_revision": revision, "chromium_version": actual_version,
            "deps_sha256": deps_digest, "build_verified": False,
            "dependencies_synced": False, "patches_verified": False,
            "working_tree_verified": False}


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("--lock", type=Path, required=True)
    parser.add_argument("--source", type=Path, required=True)
    args = parser.parse_args()
    try:
        print(json.dumps(inspect_checkout(args.source, read_lock(args.lock)), indent=2))
    except (BaselineError, UnicodeError, ValueError) as error:
        parser.exit(1, f"Baseline rejected: {error}\n")


if __name__ == "__main__":
    main()
