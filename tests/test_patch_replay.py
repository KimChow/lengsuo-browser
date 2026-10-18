"""Replayability checks for the frozen patch set.

Two failure modes have already happened and must never come back silently:

1. A patch exists in ``patches/`` but is not listed in ``baseline.json``, so the
   lock claims a baseline that cannot reproduce the tree.
2. Our own sources (``lengsuo/``) are referenced by upstream edits but never
   shipped in any patch, so ``git apply`` yields a tree that cannot link.

These tests are pure text checks: they need no Chromium checkout.
"""
import hashlib
import json
import pathlib
import re
import unittest

ROOT = pathlib.Path(__file__).resolve().parents[1]
PATCHES = ROOT / "patches"
LOCK = ROOT / "baseline.json"


def _lock():
    return json.loads(LOCK.read_text(encoding="utf-8"))


def _patch_files():
    return sorted(PATCHES.glob("*.patch"))


def _added_or_modified_paths(patch_text):
    return [
        line[len("+++ b/"):].strip()
        for line in patch_text.splitlines()
        if line.startswith("+++ b/")
    ]


class PatchLockTests(unittest.TestCase):
    def test_every_patch_is_locked(self):
        locked = {entry["path"] for entry in _lock()["patches"]}
        on_disk = {p.relative_to(ROOT).as_posix() for p in _patch_files()}
        self.assertEqual(
            sorted(on_disk - locked), [],
            "patches present but not listed in baseline.json")
        self.assertEqual(
            sorted(locked - on_disk), [],
            "baseline.json lists patches that do not exist")
        # The lock must not carry duplicates either.
        self.assertEqual(len(locked), len(_lock()["patches"]))

    def test_locked_digests_match(self):
        for entry in _lock()["patches"]:
            path = ROOT / entry["path"]
            digest = hashlib.sha256(path.read_bytes()).hexdigest()
            self.assertEqual(digest, entry["sha256"], entry["path"])

    def test_lengsuo_sources_are_shipped(self):
        """Every lengsuo/ file the tree needs must come from some patch."""
        shipped = set()
        for patch in _patch_files():
            for path in _added_or_modified_paths(
                    patch.read_text(encoding="utf-8", errors="replace")):
                if path.startswith("lengsuo/"):
                    shipped.add(path)
        # Sources the build links against; keep in sync with lengsuo/**/BUILD.gn.
        required = {
            "lengsuo/native/BUILD.gn",
            "lengsuo/native/canvas_apply.cc",
            "lengsuo/native/canvas_apply.h",
            "lengsuo/native/canvas_noise.cc",
            "lengsuo/native/canvas_noise.h",
            "lengsuo/native/fp_overrides.cc",
            "lengsuo/native/fp_overrides.h",
            "lengsuo/native/golden.inc",
            "lengsuo/native/profile_catalog.cc",
            "lengsuo/native/profile_catalog.h",
            "lengsuo/native/profile_wire.cc",
            "lengsuo/native/profile_wire.h",
            "lengsuo/native/wire_reader.h",
            "lengsuo/integration/BUILD.gn",
            "lengsuo/integration/browser_bootstrap.cc",
            "lengsuo/integration/browser_bootstrap.h",
            "lengsuo/integration/profile_bootstrap_state.cc",
            "lengsuo/integration/profile_bootstrap_state.h",
        }
        missing = sorted(required - shipped)
        self.assertEqual(missing, [], "sources never shipped by any patch")

    def test_no_patch_ships_the_same_file_twice(self):
        owners = {}
        for patch in _patch_files():
            for path in _added_or_modified_paths(
                    patch.read_text(encoding="utf-8", errors="replace")):
                if path.startswith("lengsuo/"):
                    owners.setdefault(path, []).append(patch.name)
        duplicated = {k: v for k, v in owners.items() if len(v) > 1}
        self.assertEqual(duplicated, {}, "file shipped by multiple patches")


class PatchContentTests(unittest.TestCase):
    def test_patches_target_the_locked_revision(self):
        lock = _lock()
        self.assertRegex(lock["chromium_revision"], r"^[0-9a-f]{40}$")
        self.assertTrue(lock["deps_sha256"])

    def test_no_patch_touches_credentials_or_build_output(self):
        forbidden = re.compile(
            r"^(\+\+\+ b/)?(out/|build/|.*\.keystore|.*\.pem|.*\.p12)",
            re.MULTILINE)
        for patch in _patch_files():
            text = patch.read_text(encoding="utf-8", errors="replace")
            self.assertIsNone(forbidden.search(text), patch.name)


if __name__ == "__main__":
    unittest.main()
