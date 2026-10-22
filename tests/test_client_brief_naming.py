"""A client's brief must be named exactly like the card it belongs to.

Why this exists
---------------
The client line's runner reads the brief by card name:
``brief = (ROOT / "tasks" / f"{name}.txt").read_text()``
in ``bounded_worker_runner.py``. So the brief's file name *is* the card
name -- there is no mapping table in between.

The M0 card broke that rule: its brief was called
``client-m0-machine-code-align.txt`` while the card it describes is
``m0-machine-code-align`` (which is also what the brief's own first
line says). Dispatching it would have failed with a
``FileNotFoundError`` before the session ever started.

That is the same trap the P0 card fell into earlier, and it is silent:
the mismatch is only visible at dispatch time, and the failure message
points at the missing file rather than at the naming rule.

What is checked
---------------
For every brief kept in ``docs/task-briefs/``:
- the file name, minus the ``.txt``, equals the card name declared in
  the brief's first line;
- the first line actually declares a card name (``任务：<name> —— ...``).

The client line's briefs live outside the repository -- the repository
keeps ``docs/`` local-only -- so this test reads the local copies and
skips when they are absent rather than failing a clone that has none.
"""
import pathlib
import re
import unittest

ROOT = pathlib.Path(__file__).resolve().parents[1]
BRIEFS = ROOT / "docs" / "task-briefs"

# The first line names the card, then a dash and the title:
#   任务：m0-machine-code-align —— 机器码取值跨仓对齐（客户端侧，…）
CARD_NAME = re.compile(r"^任务：([A-Za-z0-9._-]+)\s*——")


class ClientBriefNamesItsCard(unittest.TestCase):
    def test_briefs_present_or_absent_together(self):
        """A missing brief directory is not a failure -- a clone may lack it."""
        if not BRIEFS.is_dir():
            self.skipTest("the repository does not ship the client briefs")

    def test_file_name_matches_the_declared_card_name(self):
        """The runner looks the brief up by card name, so they must agree."""
        if not BRIEFS.is_dir():
            self.skipTest("the repository does not ship the client briefs")
        for path in sorted(BRIEFS.glob("*.txt")):
            with self.subTest(brief=path.name):
                first = path.read_text(encoding="utf-8").splitlines()[0]
                match = CARD_NAME.match(first)
                self.assertIsNotNone(
                    match,
                    "%s does not start with '任务：<card> ——': %r"
                    % (path.name, first))
                declared = match.group(1)
                self.assertEqual(
                    declared, path.stem,
                    "%s declares the card %r; the runner would look for "
                    "tasks/%s.txt" % (path.name, declared, declared))


if __name__ == "__main__":
    unittest.main()
