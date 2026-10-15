"""Exercise untrusted locks and real local Git objects without network access."""

import hashlib
import json
from pathlib import Path
import subprocess
import tempfile
import unittest

from tools.baseline import BaselineError, inspect_checkout, read_lock, validate_lock
from unittest.mock import patch


def fixture():
    return {
        "schema_version": 1,
        "upstream_url": "https://chromium.googlesource.com/chromium/src.git",
        "chromium_version": "155.0.8059.85",
        "chromium_revision": "0e66b94549a8bd85f03fbdf189b752d6e7b63c32",
        "deps_sha256": "a" * 64,
        "target_platforms": ["windows", "linux"],
        "patches": [],
    }


class LockTests(unittest.TestCase):
    def test_accepts_exact_pinned_baseline(self):
        raw = fixture()
        self.assertEqual(validate_lock(raw), raw)

    def test_rejects_unknown_missing_and_wrong_fields(self):
        invalid = [[], None, {**fixture(), "schema_version": True}]
        invalid += [{**fixture(), "extra": "ignored?"}]
        for key in fixture():
            raw = fixture()
            del raw[key]
            invalid.append(raw)
        for raw in invalid:
            with self.subTest(raw=raw), self.assertRaises(BaselineError):
                validate_lock(raw)

    def test_rejects_moving_ref_or_alternate_source(self):
        for key, values in {
            "upstream_url": ["https://evil.example/src.git", "file:///tmp/repo"],
            "chromium_revision": ["main", "A" * 40, "f" * 39],
            "chromium_version": ["155", "155.0.8059.85;echo", "01.0.1.2"],
            "deps_sha256": ["A" * 64, "f" * 63],
            "target_platforms": [[], ["windows", "windows"], ["unknown"]],
        }.items():
            for value in values:
                with self.subTest(key=key, value=value), self.assertRaises(BaselineError):
                    validate_lock({**fixture(), key: value})

    def test_rejects_unsafe_duplicate_or_malformed_patch_entries(self):
        good = {"path": "patches/001-screen.patch", "sha256": "b" * 64}
        self.assertEqual(validate_lock({**fixture(), "patches": [good]})["patches"], [good])
        for entry in [
            {**good, "path": "../escape.patch"},
            {**good, "path": "patches/../escape.patch"},
            {**good, "path": "/tmp/escape.patch"},
            {**good, "path": "patches\\escape.patch"},
            {**good, "path": "patches/a:b.patch"},
            {**good, "sha256": "x" * 64},
            {**good, "url": "https://example.test"},
        ]:
            with self.subTest(entry=entry), self.assertRaises(BaselineError):
                validate_lock({**fixture(), "patches": [entry]})
        with self.assertRaises(BaselineError):
            validate_lock({**fixture(), "patches": [good, good]})

    def test_duplicate_json_keys_are_rejected(self):
        with tempfile.TemporaryDirectory() as temp:
            path = Path(temp) / "lock.json"
            text = json.dumps(fixture()).replace('"schema_version": 1', '"schema_version": 2, "schema_version": 1')
            path.write_text(text)
            with self.assertRaises(BaselineError):
                read_lock(path)

    def test_reads_valid_json_and_rejects_malformed_or_oversized_input(self):
        with tempfile.TemporaryDirectory() as temp:
            path = Path(temp) / "lock.json"
            path.write_text(json.dumps(fixture()))
            self.assertEqual(read_lock(path), fixture())
            for content in [b"{", b"\xff", b" " * (1024 * 1024 + 1)]:
                path.write_bytes(content)
                with self.subTest(size=len(content)), self.assertRaises(BaselineError):
                    read_lock(path)

    def test_git_timeout_is_reported_as_failure(self):
        with patch("tools.baseline.subprocess.run", side_effect=subprocess.TimeoutExpired("git", 45)) as run:
            with self.assertRaises(BaselineError):
                inspect_checkout(Path("unused"), fixture())
            self.assertEqual(run.call_args.kwargs["timeout"], 45)
            self.assertEqual(run.call_args.kwargs["env"]["GIT_NO_LAZY_FETCH"], "1")


class CheckoutTests(unittest.TestCase):
    def setUp(self):
        self.temp = tempfile.TemporaryDirectory()
        self.addCleanup(self.temp.cleanup)
        self.repo = Path(self.temp.name)
        self.git("init", "-q")
        self.git("config", "user.name", "Test Author")
        self.git("config", "user.email", "test@example.invalid")
        (self.repo / "chrome").mkdir()
        (self.repo / "chrome/VERSION").write_text("MAJOR=155\nMINOR=0\nBUILD=8059\nPATCH=85\n")
        self.deps = b"vars = {'test_only': True}\n"
        (self.repo / "DEPS").write_bytes(self.deps)
        self.git("add", "chrome/VERSION", "DEPS")
        self.git("commit", "-qm", "test baseline")
        self.git("tag", "155.0.8059.85")
        self.git("remote", "add", "origin", fixture()["upstream_url"])
        self.lock = {**fixture(), "chromium_revision": self.git("rev-parse", "HEAD").strip(),
                     "deps_sha256": hashlib.sha256(self.deps).hexdigest()}

    def git(self, *args):
        return subprocess.check_output(["git", "-C", str(self.repo), *args], timeout=10, text=True)

    def test_matching_real_git_objects(self):
        result = inspect_checkout(self.repo, self.lock)
        self.assertEqual(result["chromium_revision"], self.lock["chromium_revision"])
        self.assertEqual(result["deps_sha256"], self.lock["deps_sha256"])
        self.assertFalse(result["build_verified"])

    def test_rejects_revision_deps_version_origin_and_tag_mismatch(self):
        for key, value in [("chromium_revision", "f" * 40), ("deps_sha256", "f" * 64),
                           ("chromium_version", "155.0.8059.86")]:
            with self.subTest(key=key), self.assertRaises(BaselineError):
                inspect_checkout(self.repo, {**self.lock, key: value})
        self.git("remote", "set-url", "origin", "https://evil.example/repo.git")
        with self.assertRaises(BaselineError):
            inspect_checkout(self.repo, self.lock)
        self.git("remote", "set-url", "origin", self.lock["upstream_url"])
        self.git("tag", "-d", "155.0.8059.85")
        with self.assertRaises(BaselineError):
            inspect_checkout(self.repo, self.lock)

    def test_detects_wrong_version_blob_even_when_tag_and_head_match(self):
        (self.repo / "chrome/VERSION").write_text("MAJOR=154\nMINOR=0\nBUILD=8059\nPATCH=85\n")
        self.git("add", "chrome/VERSION")
        self.git("commit", "-qm", "wrong source version")
        self.git("tag", "-f", "155.0.8059.85")
        lock = {**self.lock, "chromium_revision": self.git("rev-parse", "HEAD").strip()}
        with self.assertRaisesRegex(BaselineError, "Source version"):
            inspect_checkout(self.repo, lock)


if __name__ == "__main__":
    unittest.main()
