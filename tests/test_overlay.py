"""Isolated synthetic Git fixtures; never mutates the real Chromium checkout."""
import contextlib
import hashlib
import io
import json
import os
from pathlib import Path
import re
import subprocess
import tempfile
import unittest
from unittest.mock import patch

from tools.baseline import UPSTREAM
from tools.overlay import MAPPING, execute


@unittest.skipUnless(os.name == "posix" and hasattr(os, "O_NOFOLLOW"),
                     "POSIX no-follow apply and symlink fixtures")
class OverlayTests(unittest.TestCase):
    def setUp(self):
        self.temporary = tempfile.TemporaryDirectory()
        self.addCleanup(self.temporary.cleanup)
        self.root = Path(self.temporary.name)
        self.source = self.root / "source"
        self.repository = self.root / "kernel"
        self.source.mkdir()
        self.repository.mkdir()
        self.git("init", "-q")
        self.git("config", "user.name", "Fixture")
        self.git("config", "user.email", "fixture@example.invalid")
        self.git("remote", "add", "origin", UPSTREAM)
        (self.source / "chrome").mkdir()
        (self.source / "chrome/VERSION").write_text("MAJOR=155\nMINOR=0\nBUILD=1\nPATCH=2\n")
        (self.source / "DEPS").write_text("fixture only\n")
        self.git("add", ".")
        self.git("commit", "-qm", "fixture")
        self.git("tag", "155.0.1.2")
        self.lock = {
            "schema_version": 1, "upstream_url": UPSTREAM,
            "chromium_version": "155.0.1.2",
            "chromium_revision": self.git("rev-parse", "HEAD").strip(),
            "deps_sha256": hashlib.sha256((self.source / "DEPS").read_bytes()).hexdigest(),
            "target_platforms": ["linux"], "patches": [],
        }
        for origin, _ in MAPPING:
            file = self.repository / origin
            # The mapping names files in nested directories, so the
            # parent has to be created all the way down. Without it the
            # fixture fails to set up and every test in the class errors
            # out before it reaches the behaviour it is about.
            file.parent.mkdir(parents=True, exist_ok=True)
            file.write_text("fixture " + origin + "\n")

    def git(self, *args):
        return subprocess.check_output(["git", "-C", str(self.source), *args], text=True)

    def run_overlay(self, apply=False):
        return execute(self.source, self.repository, self.lock, apply)

    def test_integration_targets_included(self):
        destinations = {target for _, target in MAPPING}
        self.assertTrue(destinations.issuperset({
            "lengsuo/integration/browser_bootstrap.cc",
            "lengsuo/integration/browser_bootstrap.h",
            "lengsuo/integration/profile_bootstrap_state.cc",
            "lengsuo/integration/profile_bootstrap_state.h",
            "lengsuo/integration/browser_bootstrap_unittest.cc",
            "lengsuo/integration/BUILD.gn",
            "lengsuo/native/golden.inc",
        }))
        origins = {origin for origin, _ in MAPPING}
        self.assertNotIn("integration/profile_bootstrap_state_test.cc", origins)

    def test_gn_targets_receipt(self):
        result = self.run_overlay()
        self.assertEqual(result["gn_target"], "//lengsuo/native:profile_wire")
        self.assertEqual(result["gn_targets"], [
            "//lengsuo/integration:bootstrap", "//lengsuo/native:profile_wire",
        ])

    def test_patched_baseline_requires_explicit_override(self):
        self.lock["patches"] = [{"path": "patches/0001-x.patch", "sha256": "0" * 64}]
        self.assertEqual(self.run_overlay(True)["status"], "failed")
        self.assertFalse((self.source / "lengsuo").exists())
        result = execute(self.source, self.repository, self.lock, True,
                         allow_patched_baseline=True)
        self.assertEqual(result["status"], "applied")
        again = execute(self.source, self.repository, self.lock, False,
                        allow_patched_baseline=True)
        self.assertEqual(again["status"], "planned")

    def test_read_only_plan_and_exact_receipt(self):
        result = self.run_overlay()
        self.assertEqual(result["status"], "planned")
        self.assertFalse((self.source / "lengsuo").exists())
        self.assertEqual(result["gn_target"], "//lengsuo/native:profile_wire")
        for entry, (origin, target) in zip(result["files"], MAPPING):
            self.assertEqual(entry["source"], origin)
            self.assertEqual(entry["destination"], target)
            self.assertEqual(entry["sha256"], hashlib.sha256(
                (self.repository / origin).read_bytes()).hexdigest())
        self.assertFalse(result["browser_integrated"])
        self.assertFalse(result["build_verified"])
        self.assertFalse(result["cross_file_atomic"])

    def test_apply_and_idempotency(self):
        result = self.run_overlay(True)
        self.assertEqual(result["status"], "applied")
        for origin, target in MAPPING:
            self.assertEqual((self.source / target).read_bytes(),
                             (self.repository / origin).read_bytes())
        again = self.run_overlay(True)
        self.assertEqual(again["status"], "applied")
        self.assertTrue(all(e["action"] == "identical" for e in again["files"]))

    def test_mismatched_head_rejected(self):
        (self.source / "extra").write_text("x")
        self.git("add", ".")
        self.git("commit", "-qm", "different")
        self.assertEqual(self.run_overlay(True)["status"], "failed")
        self.assertFalse((self.source / "lengsuo").exists())

    def test_origin_tag_and_deps_validation_reused(self):
        for field in ("upstream_url", "deps_sha256", "chromium_version"):
            original = self.lock[field]
            self.lock[field] = "0" * 64 if field == "deps_sha256" else "wrong"
            self.assertEqual(self.run_overlay(True)["status"], "failed")
            self.lock[field] = original

    def test_conflict_preflight_prevents_partial_write(self):
        target = self.source / MAPPING[-1][1]
        target.parent.mkdir(parents=True)
        target.write_text("different")
        self.assertEqual(self.run_overlay(True)["status"], "failed")
        self.assertFalse((self.source / MAPPING[0][1]).exists())
        self.assertEqual(target.read_text(), "different")

    def test_dirty_tracked_target_rejected_even_if_identical(self):
        self.run_overlay(True)
        self.git("add", "lengsuo")
        self.git("commit", "-qm", "tracked overlay")
        self.lock["chromium_revision"] = self.git("rev-parse", "HEAD").strip()
        self.git("tag", "-f", "155.0.1.2")
        origin, target = MAPPING[0]
        (self.repository / origin).write_text("changed")
        (self.source / target).write_text("changed")
        self.assertEqual(self.run_overlay(True)["status"], "failed")

    def test_symlink_ancestors_and_file_rejected(self):
        outside = self.root / "outside"
        outside.mkdir()
        (self.source / "lengsuo").symlink_to(outside, target_is_directory=True)
        self.assertEqual(self.run_overlay(True)["status"], "failed")
        self.assertEqual(list(outside.iterdir()), [])
        (self.source / "lengsuo").unlink()
        target = self.source / MAPPING[0][1]
        target.parent.mkdir(parents=True)
        target.symlink_to(self.repository / MAPPING[0][0])
        self.assertEqual(self.run_overlay(True)["status"], "failed")

    def test_source_symlink_rejected(self):
        source = self.repository / MAPPING[0][0]
        source.unlink()
        source.symlink_to(self.source / "DEPS")
        self.assertEqual(self.run_overlay(True)["status"], "failed")

    def test_escape_mapping_rejected(self):
        for name in ("../escape", "/tmp/escape", "x/../../escape", "C:/escape", "x\\escape"):
            with patch("tools.overlay.MAPPING", (("native/profile_wire.h", name),)):
                self.assertEqual(self.run_overlay(True)["status"], "failed")
        self.assertFalse((self.root / "escape").exists())

    def test_partial_io_failure_receipt(self):
        from tools.overlay import _atomic_add
        count = 0
        def fail_second(root, name, data):
            nonlocal count
            count += 1
            if count == 2:
                raise OSError("injected publication failure")
            _atomic_add(root, name, data)
        with patch("tools.overlay._atomic_add", side_effect=fail_second):
            result = self.run_overlay(True)
        self.assertEqual(result["status"], "failed")
        self.assertEqual(result["files"][0]["action"], "created")
        self.assertEqual(result["files"][1]["action"], "create")
        self.assertTrue((self.source / MAPPING[0][1]).exists())
        self.assertFalse((self.source / MAPPING[1][1]).exists())
        self.assertFalse(list((self.source / "lengsuo/native").glob(".lengsuo-overlay-*")))
        self.assertEqual(self.run_overlay(True)["status"], "applied")

    def test_cli_defaults_to_plan_and_failure_is_json(self):
        from tools.overlay import main
        lock_path = self.root / "baseline.json"
        lock_path.write_text(json.dumps(self.lock))
        arguments = ["overlay", "--source", str(self.source), "--repository",
                     str(self.repository), "--lock", str(lock_path)]
        output = io.StringIO()
        with patch("sys.argv", arguments), contextlib.redirect_stdout(output):
            self.assertEqual(main(), 0)
        self.assertEqual(json.loads(output.getvalue())["mode"], "plan")
        self.assertFalse((self.source / "lengsuo").exists())
        lock_path.write_text("invalid json")
        output = io.StringIO()
        with patch("sys.argv", arguments), contextlib.redirect_stdout(output):
            self.assertEqual(main(), 1)
        self.assertEqual(json.loads(output.getvalue())["status"], "failed")

    def test_nested_checkout_root_rejected(self):
        result = execute(self.source / "chrome", self.repository, self.lock, True)
        self.assertEqual(result["status"], "failed")
        self.assertFalse((self.source / "chrome/lengsuo").exists())

    def test_racing_symlink_ancestor_rejected_at_publication(self):
        from tools.overlay import _atomic_add
        outside = self.root / "outside"
        outside.mkdir()
        def race(root, name, data):
            (Path(root) / "lengsuo").symlink_to(outside, target_is_directory=True)
            _atomic_add(root, name, data)
        with patch("tools.overlay._atomic_add", side_effect=race):
            result = self.run_overlay(True)
        self.assertEqual(result["status"], "failed")
        self.assertEqual(list(outside.iterdir()), [])
    def test_racing_different_file_not_overwritten(self):
        from tools.overlay import _atomic_add
        def race(root, name, data):
            target = Path(root) / name
            target.parent.mkdir(parents=True, exist_ok=True)
            target.write_text("racing writer")
            _atomic_add(root, name, data)
        with patch("tools.overlay._atomic_add", side_effect=race):
            result = self.run_overlay(True)
        self.assertEqual(result["status"], "failed")
        self.assertEqual((self.source / MAPPING[0][1]).read_text(), "racing writer")


class UnsupportedApplyTests(unittest.TestCase):
    def test_unsupported_platform_explicitly_rejected_before_baseline(self):
        with patch("tools.overlay._supports_apply", return_value=False):
            result = execute(".", ".", {}, True)
        self.assertEqual(result["status"], "failed")
        self.assertIn("requires POSIX", result["error"])
        self.assertEqual(result["files"], [])


class OverlayMappingInvariant(unittest.TestCase):
    def test_native_build_sources_all_mapped(self):
        text = (Path(__file__).resolve().parent.parent
                / "native/BUILD.gn").read_text()
        referenced = set()
        for block in re.findall(r"sources\s*=\s*\[(.*?)\]", text, re.S):
            referenced |= {item.strip().strip('",') for item in block.split("\n")
                           if item.strip().strip('",')}
        mapped = {Path(target).name for _, target in MAPPING
                  if target.startswith("lengsuo/native/")}
        self.assertTrue(referenced, "native/BUILD.gn declares no sources")
        self.assertTrue(referenced <= mapped,
                        "native/BUILD.gn sources missing from MAPPING: "
                        + str(referenced - mapped))

if __name__ == "__main__":
    unittest.main()
