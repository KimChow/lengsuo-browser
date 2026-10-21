"""Checks that the dispatch script points each task at the right brief.

The dispatch script picks a brief from the task letter. When a new round adds
a task, forgetting the mapping silently sends the agent to an older round's
brief: it then implements the wrong thing (or reports "task O not found") and
the round is wasted. That is a text-level bug, so it is caught here without a
Chromium checkout.
"""
import pathlib
import re
import unittest

ROOT = pathlib.Path(__file__).resolve().parents[1]
LAUNCHER = ROOT / "tools" / "rebuild" / "launch_l3b_short.sh"
DOCS = ROOT / "docs"


def _launcher_text():
    return LAUNCHER.read_text(encoding="utf-8")


def _task_to_brief():
    """Parses the `case "$task" in ... esac` block into {task: brief path}."""
    text = _launcher_text()
    block = re.search(r'case "\$task" in(.*?)esac', text, re.S)
    if not block:
        raise AssertionError("dispatch script lost its task->brief case block")
    mapping = {}
    for line in block.group(1).splitlines():
        match = re.match(
            r'\s*([A-Z][A-Z|]*)\)\s*task_brief="\$REPO_SRC/([^"]+)"', line)
        if not match:
            continue
        brief = match.group(2)
        for task in match.group(1).split("|"):
            mapping[task] = brief
    return mapping


class DispatchBriefTests(unittest.TestCase):
    def test_every_round_brief_exists(self):
        mapping = _task_to_brief()
        self.assertTrue(mapping, "no task->brief mapping parsed")
        for task, brief in sorted(mapping.items()):
            with self.subTest(task=task):
                self.assertTrue(
                    (ROOT / brief).is_file(),
                    "task %s points at missing brief %s" % (task, brief))

    def test_every_live_brief_is_reachable_from_some_task(self):
        """A live brief no task maps to means a round can never start.

        Rounds land as docs/cc-task-brief*.md; if a new one is committed but the
        dispatch script still sends every task to an older round, the round
        never starts. Finished rounds are listed below and exempt: their tasks
        are merged and will not be re-dispatched.
        """
        finished = {
            "cc-task-brief.md",       # round one (A-H, merged)
            "cc-task-brief-l3.md",    # round one, storage layer (merged)
            "cc-task-brief-l3b.md",   # round two I/J/K/L (merged)
        }
        mapping = _task_to_brief()
        reachable = {pathlib.PurePosixPath(b).name for b in mapping.values()}
        shipped = {p.name for p in DOCS.glob("cc-task-brief*.md")}
        self.assertEqual(
            shipped - reachable - finished, set(),
            "briefs exist but no task letter points at them; add a case "
            "in launch_l3b_short.sh")

    def test_third_round_tasks_use_the_third_round_brief(self):
        """Guards the specific regression: M/N briefly mapped to round two."""
        mapping = _task_to_brief()
        self.assertEqual(mapping.get("M"), "docs/cc-task-brief-l3d.md")

    def test_fourth_round_task_does_not_fall_back(self):
        """O must not hit the default arm (round two's brief)."""
        mapping = _task_to_brief()
        self.assertIn("O", mapping)
        self.assertEqual(mapping["O"], "docs/cc-task-brief-l3e.md")
        self.assertNotIn(mapping["O"], ("docs/cc-task-brief-l3.md",
                                        "docs/cc-task-brief-l3b.md"))

    def test_fifth_round_tasks_use_the_fifth_round_brief(self):
        """N/Q/R are the fifth round; P/S live in the client repo, not here."""
        mapping = _task_to_brief()
        for task in ("N", "Q", "R"):
            with self.subTest(task=task):
                self.assertEqual(mapping.get(task),
                                 "docs/cc-task-brief-l3f.md")
        self.assertNotIn("P", mapping,
                         "task P belongs to the client repo line")

    def test_n_does_not_point_at_the_third_round_brief(self):
        """N moved to the fifth round; the third round would redo finished work.

        Round three's brief asks for the envelope to be written from
        scratch. That work is already done (18 gtest, commit 2f0ea73), so
        dispatching N against it would burn a card rebuilding it.
        """
        mapping = _task_to_brief()
        self.assertNotEqual(mapping.get("N"), "docs/cc-task-brief-l3d.md")

    def test_fifth_round_brief_has_a_section_for_every_mapped_task(self):
        """Each task the script can dispatch needs its own section.

        A brief without the task's section sends the card off to read
        another round's instructions -- the exact failure the mapping
        above is meant to prevent.
        """
        mapping = _task_to_brief()
        text = (DOCS / "cc-task-brief-l3f.md").read_text(encoding="utf-8")
        for task in ("N", "Q", "R"):
            if mapping.get(task) != "docs/cc-task-brief-l3f.md":
                continue
            with self.subTest(task=task):
                self.assertRegex(text, r"(?m)^## 任务 %s[：: ]" % task)

    def test_wire_contract_exists(self):
        """P and Q are two repos; the shared contract must live in the repo."""
        contract = ROOT / "docs" / "kernel-wire-contract.md"
        self.assertTrue(contract.is_file())
        text = contract.read_text(encoding="utf-8")
        for needed in ("--lengsuo-kernel-pipe", "Hello", "KeyEnvelope",
                       "0x81", "CanvasSample", "大端"):
            with self.subTest(needed=needed):
                self.assertIn(needed, text)

    def test_round_four_brief_names_its_task(self):
        """The brief itself must state the task letter the prompt will ask for."""
        brief = (DOCS / "cc-task-brief-l3e.md").read_text(encoding="utf-8")
        self.assertRegex(brief, r"(?m)^## 任务 O[：: ]")
        self.assertIn("允许改动的文件", brief)
        self.assertIn("report.md", brief)


if __name__ == "__main__":
    unittest.main()
