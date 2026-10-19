"""The overlay sources and the sources captured in patch 0011 must be identical.

Both paths exist on purpose: ``tools/overlay.py`` copies the working files into
a Chromium tree for fast iteration, while ``patches/0011-lengsuo-sources.patch``
is what a clean rebuild replays. If they drift, an incremental build and a
from-scratch build produce different browsers — and the difference is invisible
until something fails to link.

This happened once: the newer integration sources were copied to
``native/integration/`` while the overlay still read ``integration/``, so the
patch and the overlay disagreed. These tests make that impossible to miss.
"""

import pathlib
import re
import unittest

ROOT = pathlib.Path(__file__).resolve().parents[1]
SOURCE_PATCH = ROOT / "patches" / "0011-lengsuo-sources.patch"

# Where each patched path comes from inside this repository.
MIRRORS = (
    ("lengsuo/native/", "native/"),
    ("lengsuo/integration/", "integration/"),
)


def _sections(patch_text):
    """Yield (target_path, is_new_file, added_lines) per file in the patch."""
    lines = patch_text.splitlines()
    current = None
    new_file = False
    body = []
    for line in lines:
        if line.startswith("diff --git "):
            if current is not None:
                yield current, new_file, body
            current, new_file, body = None, False, []
            continue
        if line.startswith("new file mode"):
            new_file = True
            continue
        if line.startswith("+++ b/"):
            current = line[len("+++ b/"):]
            continue
        if line.startswith("@@") or line.startswith("--- ") or \
                line.startswith("index ") or line.startswith("Binary "):
            continue
        if current is not None and line.startswith("+"):
            body.append(line[1:])
    if current is not None:
        yield current, new_file, body


def _repo_path(target):
    for patch_prefix, repo_prefix in MIRRORS:
        if target.startswith(patch_prefix):
            return repo_prefix + target[len(patch_prefix):]
    return None


class SourceMirrorTests(unittest.TestCase):
    def test_patch_contains_our_sources(self):
        self.assertTrue(SOURCE_PATCH.exists(), "0011 patch missing")
        sections = list(_sections(SOURCE_PATCH.read_text(encoding="utf-8")))
        self.assertGreaterEqual(len(sections), 15, "patch looks truncated")

    def test_every_patched_source_matches_the_repository_copy(self):
        sections = list(_sections(SOURCE_PATCH.read_text(encoding="utf-8")))
        checked = 0
        for target, is_new, body in sections:
            repo_rel = _repo_path(target)
            if repo_rel is None:
                continue
            self.assertTrue(is_new, "%s should be a new file" % target)
            source = ROOT / repo_rel
            self.assertTrue(source.exists(),
                            "patch declares %s but %s is missing"
                            % (target, repo_rel))
            expected = "\n".join(body).rstrip("\n")
            actual = source.read_text(encoding="utf-8").rstrip("\n")
            checked += 1
            self.assertEqual(
                expected, actual,
                "%s differs from the copy captured in %s; the overlay and a "
                "clean rebuild would produce different browsers"
                % (repo_rel, SOURCE_PATCH.name))
        self.assertGreater(checked, 10, "no mirrored sources were compared")

    def test_no_duplicate_source_tree(self):
        """A second copy under native/integration/ is what caused the drift."""
        duplicate = ROOT / "native" / "integration"
        self.assertFalse(
            duplicate.exists(),
            "native/integration/ duplicates integration/; keep one copy so the "
            "overlay and the patch cannot disagree")

    def test_overlay_mapping_targets_existing_files(self):
        """Every overlay source path must resolve inside the repository."""
        text = (ROOT / "tools" / "overlay.py").read_text(encoding="utf-8")
        entries = re.findall(r'\("([^"]+\.(?:cc|h|gn|inc))",\s*"([^"]+)"\)', text)
        self.assertGreater(len(entries), 10, "overlay mapping looks empty")
        for source, _target in entries:
            self.assertTrue((ROOT / source).exists(),
                            "overlay maps missing source %s" % source)


if __name__ == "__main__":
    unittest.main()
