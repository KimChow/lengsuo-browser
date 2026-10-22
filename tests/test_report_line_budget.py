"""The root report must stay an index, not a growing pile of prose.

Why this exists
---------------
`report.md` is where a card's semantic conflicts are supposed to end up
-- the task briefs tell every card to stop and write there instead of
picking a path on its own. The dispatcher then folds those entries into
the root file, one section per task.

That worked until the sections started stacking: the root file reached
760 lines and `tools/check_line_limit.py` failed it
(`FAIL report.md: 760 > 700`). The hard limit is not negotiable, and
the failure only shows up after a commit has already landed -- the
commit that pushed the file over the line also claimed the line check
was clean.

So the root file is now only an index: it points at the per-task
conflict files under `docs/l3-reports/` and lists which items are
still open. This test pins that shape, so the next task that adds a
section has to add it to its own file and extend the index -- and the
index cannot quietly grow back into a full report.

What is checked
---------------
- The root report stays under the soft target (400 lines), well below
  the 700-line hard limit that broke it.
- It still names every task's conflict file, so the index does not
  quietly drop a task.
- It does not carry the per-task sections itself: a heading like
  `## Q-A.` belongs in the Q file, not in the index.

The per-task files live under `docs/`, which the repository does not
distribute. Their presence is therefore not required here -- a clone
without `docs/` must not fail this test. The root index is what
matters, and it is in the repository.
"""
import pathlib
import re
import unittest

ROOT = pathlib.Path(__file__).resolve().parents[1]
REPORT = ROOT / "report.md"

# The tasks whose conflicts have their own file. A new task adds a row
# to the index and a file under `docs/l3-reports/`.
TASKS = ("O", "Q", "N", "R")

# The soft target from the project's line budget. The index is meant
# to be short enough that reading it costs nothing.
SOFT_TARGET = 400


class ReportStaysAnIndex(unittest.TestCase):
    def test_root_report_is_under_the_line_budget(self):
        """The root report must not approach the 700-line hard limit."""
        self.assertTrue(REPORT.is_file(), "the root report is missing")
        lines = len(REPORT.read_text(encoding="utf-8").splitlines())
        self.assertLess(
            lines, SOFT_TARGET,
            "report.md is %d lines; it is an index and must stay under %d"
            % (lines, SOFT_TARGET))

    def test_index_points_at_every_task(self):
        """Every task's conflict file must be reachable from the index."""
        text = REPORT.read_text(encoding="utf-8")
        for task in TASKS:
            with self.subTest(task=task):
                self.assertIn("task-%s-conflicts.md" % task, text)

    def test_index_holds_no_task_sections(self):
        """A per-task section in the index means it grew back."""
        text = REPORT.read_text(encoding="utf-8")
        # `## Q-A.` and friends: a section heading that belongs to one
        # task's conflict file.
        found = re.findall(r"(?m)^##\s+[A-Z]-\d", text)
        self.assertEqual(
            [], found,
            "report.md carries task sections %s; they belong in "
            "docs/l3-reports/task-*-conflicts.md" % found)


if __name__ == "__main__":
    unittest.main()
