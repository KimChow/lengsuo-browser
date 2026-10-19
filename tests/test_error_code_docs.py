"""Error codes must never be quoted without saying whose registry they come from.

Two independent registries overlap numerically: the reference client's app error
codes and ours. `60003` means "path does not exist" in reference and "profile is
self-contradictory, refuse to start" in our private spec. A bare `60003` in a
design doc is therefore ambiguous, and an implementer reading it can silently
build the wrong behaviour.

These tests pin the convention in docs/profile-spec-v1.md and
docs/server-gpu-fields-request.md:

  * prose lines that mention an overlapping code must name the owner
  * every row of an error-code table must name the owner
  * the shared number must be marked as "same number, different meaning"
  * the codes we depend on must actually be documented

The convention exists because the ambiguity was real: our earlier doc demanded
that reference align on `60003` for profile rejection, which is not what their
`60003` does at all.
"""

import pathlib
import re
import unittest

ROOT = pathlib.Path(__file__).resolve().parents[1]
DOCS = ROOT / "docs"

# Codes that exist in more than one registry, so a bare mention is ambiguous.
OVERLAPPING = ("60003",)

# Codes whose meaning we rely on in the design; both docs must state them.
REQUIRED = ("60003", "60004", "10008", "10009")

# Markers that make ownership unambiguous on a single line.
OWNER_MARKERS = ("对标端", "我方", "reference")

# A prose line is anything that is not a table row or a fenced block.
TABLE_ROW = re.compile(r"^\s*\|")
FENCE = "```"


def _doc_paths():
    """Docs that talk about error codes, in a stable order."""
    names = ("profile-spec-v1.md", "server-gpu-fields-request.md")
    return [(name, DOCS / name) for name in names]


def _prose_lines(text):
    """Yield (line_number, line) for lines outside tables and fences."""
    in_fence = False
    for number, line in enumerate(text.splitlines(), 1):
        if line.strip().startswith(FENCE):
            in_fence = not in_fence
            continue
        if in_fence or TABLE_ROW.match(line):
            continue
        yield number, line


def _table_rows(text):
    """Yield (line_number, cells) for table rows whose first cell holds a code."""
    for number, line in enumerate(text.splitlines(), 1):
        if not TABLE_ROW.match(line):
            continue
        cells = [cell.strip() for cell in line.strip().strip("|").split("|")]
        if cells:
            yield number, cells


class ErrorCodeDocsTests(unittest.TestCase):
    def test_docs_exist(self):
        for name, path in _doc_paths():
            with self.subTest(doc=name):
                self.assertTrue(path.is_file(), "%s is missing" % name)

    def test_prose_mentions_name_the_owner(self):
        """A bare code in prose is a defect: readers cannot resolve it."""
        for name, path in _doc_paths():
            for number, line in _prose_lines(path.read_text(encoding="utf-8")):
                for code in OVERLAPPING:
                    if code not in line:
                        continue
                    with self.subTest(doc=name, line=number):
                        self.assertTrue(
                            any(marker in line for marker in OWNER_MARKERS),
                            "%s:%d quotes %s without naming the registry: %r"
                            % (name, number, code, line.strip()[:120]))

    def test_error_table_rows_name_the_owner(self):
        """In an error-code table every row must say whose code it is."""
        for name, path in _doc_paths():
            for number, cells in _table_rows(path.read_text(encoding="utf-8")):
                if not any(code in cells[0] for code in REQUIRED):
                    continue
                row = " | ".join(cells)
                with self.subTest(doc=name, line=number):
                    self.assertTrue(
                        any(marker in row for marker in OWNER_MARKERS),
                        "%s:%d has an error-code row without an owner: %r"
                        % (name, number, row[:120]))

    def test_shared_code_is_flagged_as_different_meaning(self):
        """The whole point: one number, two meanings, stated explicitly."""
        for name, path in _doc_paths():
            text = path.read_text(encoding="utf-8")
            with self.subTest(doc=name):
                self.assertIn("60003", text)
                self.assertTrue(
                    "同号不同义" in text or "same number" in text.lower(),
                    "%s must state that 60003 has two meanings" % name)

    def test_required_codes_are_documented(self):
        for name, path in _doc_paths():
            text = path.read_text(encoding="utf-8")
            for code in REQUIRED:
                with self.subTest(doc=name, code=code):
                    self.assertIn(code, text, "%s never mentions %s" % (name, code))

    def test_our_verification_failure_has_its_own_code(self):
        """Signature failures must not reuse a reference number."""
        text = (DOCS / "profile-spec-v1.md").read_text(encoding="utf-8")
        self.assertIn("60004(我方", text,
                      "signature failure must use its own qualified code")


if __name__ == "__main__":
    unittest.main()
