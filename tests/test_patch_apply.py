"""Isolated synthetic Git fixtures for the reviewed baseline patch applicator."""
import contextlib
import hashlib
import io
import json
import os
from pathlib import Path
import subprocess
import tempfile
import unittest
from unittest.mock import patch

from tools.baseline import UPSTREAM

MODIFY_PATCH = """--- a/base/foo.cc
+++ b/base/foo.cc
@@ -1 +1,2 @@
 upstream line
+lengsuo fixture line
"""

CREATE_PATCH = """--- /dev/null
+++ b/content/bar.cc
@@ -0,0 +1 @@
+new fixture file
"""


@unittest.skipUnless(os.name == "posix", "Git apply fixtures run on POSIX")
class PatchApplyTests(unittest.TestCase):
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
        (self.source / "chrome/VERSION").write_text(
            "MAJOR=155\nMINOR=0\nBUILD=1\nPATCH=2\n")
        (self.source / "DEPS").write_text("fixture only\n")
        (self.source / "base").mkdir()
        (self.source / "base/foo.cc").write_text("upstream line\n")
        self.git("add", ".")
        self.git("commit", "-qm", "fixture")
        self.git("tag", "155.0.1.2")
        self.write_patch("0001-fixture-modify.patch", MODIFY_PATCH)
        self.write_patch("0002-fixture-create.patch", CREATE_PATCH)
        self.lock = {
            "schema_version": 1, "upstream_url": UPSTREAM,
            "chromium_version": "155.0.1.2",
            "chromium_revision": self.git("rev-parse", "HEAD").strip(),
            "deps_sha256": hashlib.sha256(
                (self.source / "DEPS").read_bytes()).hexdigest(),
            "target_platforms": ["linux"],
            "patches": [
                {"path": "patches/0001-fixture-modify.patch",
                 "sha256": self.digest("0001-fixture-modify.patch")},
                {"path": "patches/0002-fixture-create.patch",
                 "sha256": self.digest("0002-fixture-create.patch")},
            ],
        }

    def write_patch(self, name, body):
        directory = self.repository / "patches"
        directory.mkdir(exist_ok=True)
        (directory / name).write_text(body)

    def digest(self, name):
        return hashlib.sha256(
            (self.repository / "patches" / name).read_bytes()).hexdigest()

    def git(self, *args):
        return subprocess.check_output(
            ["git", "-C", str(self.source), *args], text=True)

    def run_tool(self, apply=False):
        from tools.patch_apply import execute
        return execute(self.source, self.repository, self.lock, apply)

    def test_plan_checks_without_modification(self):
        result = self.run_tool()
        self.assertEqual(result["status"], "planned")
        self.assertEqual([p["action"] for p in result["patches"]],
                         ["check-passed", "check-passed"])
        self.assertEqual((self.source / "base/foo.cc").read_text(),
                         "upstream line\n")
        self.assertFalse((self.source / "content/bar.cc").exists())
        self.assertFalse(result["upstream_tree_modified"])
        self.assertFalse(result["build_verified"])
        self.assertFalse(result["browser_integrated"])
        self.assertNotIn("overlay", result)

    def test_apply_patches_and_idempotent_rerun(self):
        result = self.run_tool(True)
        self.assertEqual(result["status"], "applied")
        self.assertEqual([p["action"] for p in result["patches"]],
                         ["applied", "applied"])
        self.assertEqual((self.source / "base/foo.cc").read_text(),
                         "upstream line\nlengsuo fixture line\n")
        self.assertEqual((self.source / "content/bar.cc").read_text(),
                         "new fixture file\n")
        again = self.run_tool(True)
        self.assertEqual(again["status"], "applied")
        self.assertEqual([p["action"] for p in again["patches"]],
                         ["already-applied", "already-applied"])
        planned = self.run_tool()
        self.assertEqual(planned["status"], "applied")

    def test_empty_patch_list_rejected(self):
        self.lock["patches"] = []
        self.assertEqual(self.run_tool(True)["status"], "failed")
        self.assertEqual((self.source / "base/foo.cc").read_text(),
                         "upstream line\n")

    def test_patch_directory_mismatch_rejected(self):
        (self.repository / "patches/0002-fixture-create.patch").unlink()
        self.assertEqual(self.run_tool(True)["status"], "failed")
        extra = self.repository / "patches/0003-fixture-unexpected.patch"
        extra.write_text(CREATE_PATCH)
        self.assertEqual(self.run_tool(True)["status"], "failed")

    def test_digest_mismatch_rejected(self):
        self.write_patch("0001-fixture-modify.patch",
                         MODIFY_PATCH.replace("lengsuo", "tampered"))
        self.assertEqual(self.run_tool(True)["status"], "failed")
        self.assertEqual((self.source / "base/foo.cc").read_text(),
                         "upstream line\n")

    def test_head_mismatch_rejected(self):
        (self.source / "extra").write_text("x")
        self.git("add", ".")
        self.git("commit", "-qm", "different")
        self.assertEqual(self.run_tool(True)["status"], "failed")
        self.assertEqual((self.source / "base/foo.cc").read_text(),
                         "upstream line\n")

    def test_partial_application_refused(self):
        subprocess.run(["git", "-C", str(self.source), "apply", "-"],
                       input=MODIFY_PATCH, text=True, check=True)
        result = self.run_tool(True)
        self.assertEqual(result["status"], "failed")
        self.assertFalse((self.source / "content/bar.cc").exists())
        self.assertIn("clean", result["error"])

    def test_dirty_unrelated_file_refused(self):
        (self.source / "base/other.cc").write_text("dirty tracked\n")
        self.git("add", "base/other.cc")
        result = self.run_tool(True)
        self.assertEqual(result["status"], "failed")
        self.assertIn("clean", result["error"])
        self.assertFalse((self.source / "content/bar.cc").exists())

    def test_nested_checkout_root_rejected(self):
        from tools.patch_apply import execute
        result = execute(self.source / "base", self.repository, self.lock, True)
        self.assertEqual(result["status"], "failed")
        self.assertFalse((self.source / "content/bar.cc").exists())

    def test_cli_defaults_to_plan_and_failure_is_json(self):
        from tools.patch_apply import main
        lock_path = self.root / "baseline.json"
        lock_path.write_text(json.dumps(self.lock))
        arguments = ["patch_apply", "--source", str(self.source),
                     "--repository", str(self.repository),
                     "--lock", str(lock_path)]
        output = io.StringIO()
        with patch("sys.argv", arguments), contextlib.redirect_stdout(output):
            self.assertEqual(main(), 0)
        self.assertEqual(json.loads(output.getvalue())["mode"], "plan")
        lock_path.write_text("invalid json")
        output = io.StringIO()
        with patch("sys.argv", arguments), contextlib.redirect_stdout(output):
            self.assertEqual(main(), 1)
        self.assertEqual(json.loads(output.getvalue())["status"], "failed")


if __name__ == "__main__":
    unittest.main()
