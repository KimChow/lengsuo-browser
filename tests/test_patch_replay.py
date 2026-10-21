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


def _git_blob(rel: str):
    """The repository's content for `rel`, committed content first."""
    import subprocess
    for spec in (f"HEAD:{rel}", f":{rel}"):
        try:
            return subprocess.run(
                ["git", "cat-file", "blob", spec],
                cwd=ROOT, capture_output=True, check=True).stdout
        except (subprocess.CalledProcessError, FileNotFoundError):
            continue
    return None


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
        """The lock must match the repository content, not the working tree.

        On Windows the working tree serves CRLF while the repository stores
        LF. Hashing the working tree produced a lock that the Linux build
        host could never satisfy: every patch was rejected with "digest
        differs from baseline". The digest therefore has to be taken from
        the blob git actually holds, which is what a fresh clone gives.
        """
        for entry in _lock()["patches"]:
            path = ROOT / entry["path"]
            digest = hashlib.sha256(path.read_bytes()).hexdigest()
            blob = _git_blob(entry["path"])
            if blob is None:
                # Not tracked by git: the working tree is all there is.
                self.assertEqual(digest, entry["sha256"], entry["path"])
                continue
            self.assertEqual(
                hashlib.sha256(blob).hexdigest(), entry["sha256"],
                f"{entry['path']}: lock does not match the repository content")

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
            # The L3 storage layer. These were missing from the shipped set
            # once: 0011 predated them, so the sources reached the tree
            # through the overlay while BUILD.gn never learned about them,
            # and the link failed with an undefined L3BootstrapState.
            "lengsuo/native/cache_record.cc",
            "lengsuo/native/cache_record.h",
            "lengsuo/native/extension_pin.cc",
            "lengsuo/native/extension_pin.h",
            "lengsuo/native/l3_runtime.cc",
            "lengsuo/native/l3_runtime.h",
            "lengsuo/native/l3_wire.cc",
            "lengsuo/native/l3_wire.h",
            "lengsuo/native/storage_consistency.cc",
            "lengsuo/native/storage_consistency.h",
            "lengsuo/native/storage_pin.cc",
            "lengsuo/native/storage_pin.h",
            "lengsuo/integration/l3_bootstrap.cc",
            "lengsuo/integration/l3_bootstrap.h",
            # L3 document delivery over Mojo (task O).
            "lengsuo/integration/l3_provisioning_impl.cc",
            "lengsuo/integration/l3_provisioning_impl.h",
            "lengsuo/integration/l3_provisioning_impl_test.cc",
            "lengsuo/public/mojom/BUILD.gn",
            "lengsuo/public/mojom/l3_provisioning.mojom",
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

    def test_hunk_headers_agree_on_the_target_file(self):
        """Every file section names the same path in its --- and +++ lines.

        A section whose --- and +++ disagree makes `patch` take the *old* name
        as the target while the hunk body belongs to the *new* file. That
        silently drops build-system edits into a source file: patches/0007
        shipped `--- a/chrome/browser/chrome_content_browser_client.cc` with
        `+++ b/chrome/browser/net/BUILD.gn`, which put a GN dependency line
        into the middle of a C++ include list and broke the build with
        "expected unqualified-id".

        A section that has a --- but no +++ (a leftover from when another
        patch took the file over) is rejected too: `patch` reads it as a
        malformed section and refuses the whole file.
        """
        for patch in _patch_files():
            text = patch.read_text(encoding="utf-8", errors="replace")
            old_path = None
            for line in text.splitlines():
                if line.startswith("--- "):
                    old_path = line[4:].strip()
                    if old_path == "/dev/null":
                        old_path = None
                elif line.startswith("+++ "):
                    new_path = line[4:].strip()
                    if new_path == "/dev/null":
                        new_path = None
                    if old_path is not None and new_path is not None:
                        # Strip the a/ b/ prefix so the comparison is on the
                        # repository path itself.
                        self.assertEqual(
                            old_path[2:] if old_path[:2] in ("a/", "b/") else old_path,
                            new_path[2:] if new_path[:2] in ("a/", "b/") else new_path,
                            f"{patch.name}: hunk header disagrees on target file "
                            f"({old_path} vs {new_path})")
                    old_path = None
                elif old_path is not None and line.startswith("diff --git "):
                    self.fail(
                        f"{patch.name}: file section for {old_path} has a --- "
                        f"line but no +++ line; drop the leftover section")

    def test_no_source_file_contains_gn_build_syntax(self):
        """A GN fragment inside a .cc/.h means a hunk landed in the wrong file.

        This is the same defect as the header mismatch above, caught from the
        other side: rather than trusting the header pair, look at what the
        hunk body actually contains and refuse GN-only lines in source files.
        """
        gn_only = re.compile(
            r"^\+\s*(deps|public_deps|private_deps|visibility|defines)\s*=\s*\[",
            re.MULTILINE)
        for patch in _patch_files():
            text = patch.read_text(encoding="utf-8", errors="replace")
            target = None
            for line in text.splitlines():
                if line.startswith("+++ "):
                    target = line[4:].strip()
                elif target and (target.endswith(".cc") or target.endswith(".h")):
                    if gn_only.search(line):
                        self.fail(
                            f"{patch.name}: GN build syntax added to {target}: "
                            f"{line.strip()!r}")
                    if not line.startswith(("+", "-", " ", "\\")) and line:
                        target = None

    def test_gn_sources_lists_match_the_shipped_files(self):
        """Every source in lengsuo/**/BUILD.gn must be shipped by a patch.

        A file that reaches the tree but is not listed in its BUILD.gn is
        invisible to the build: the sources compile nowhere and the link
        reports an undefined symbol. That is exactly what happened with
        lengsuo/integration/l3_bootstrap.cc - the file was present, the
        source_set did not mention it, and the browser binary failed to link
        with "undefined symbol: lengsuo::L3BootstrapState::Initialize".
        """
        shipped = set()
        for patch in _patch_files():
            for path in _added_or_modified_paths(
                    patch.read_text(encoding="utf-8", errors="replace")):
                if path.startswith("lengsuo/"):
                    shipped.add(path)

        for build_gn in sorted(ROOT.glob("*/BUILD.gn")):
            directory = f"lengsuo/{build_gn.parent.name}"
            text = build_gn.read_text(encoding="utf-8", errors="replace")
            listed = set(re.findall(r'"([A-Za-z0-9_.]+\.(?:cc|h|inc))"', text))
            for name in sorted(listed):
                relative = f"{directory}/{name}"
                self.assertIn(
                    relative, shipped,
                    f"{relative} is listed in {build_gn} but no patch ships it")


if __name__ == "__main__":
    unittest.main()
