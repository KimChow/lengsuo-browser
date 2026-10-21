"""Checks that the dispatch script points each task at the right brief.

The dispatch script picks a brief from the task letter. When a new round adds
a task, forgetting the mapping silently sends the agent to an older round's
brief: it then implements the wrong thing (or reports "task O not found") and
the round is wasted. That is a text-level bug, so it is caught here without a
Chromium checkout.
"""
import os
import pathlib
import re
import subprocess
import sys
import unittest

sys.path.insert(0, str(pathlib.Path(__file__).resolve().parent))
from bash_helper import find_bash  # noqa: E402  (path set up above)

ROOT = pathlib.Path(__file__).resolve().parents[1]
LAUNCHER = ROOT / "tools" / "rebuild" / "launch_l3b_short.sh"
DOCS = ROOT / "docs"

# ``docs/`` 只留本地（见 .gitignore），不进仓。派卡任务书在 CI 上不存在时，
# 依赖文件内容的断言整组跳过；纯脚本映射的断言仍然执行。
_skip_without_docs = unittest.skipUnless(
    DOCS.is_dir(), "docs/ 仅本地留存，仓库中不存在")


def _launcher_text():
    return LAUNCHER.read_text(encoding="utf-8")


def _resolve_briefs(tasks, repo_src=None, brief_dir=None):
    """Ask the launcher which brief each task would use.

    The launcher answers this itself, through the mode that only does the
    lookup. Asking the script is what makes the test check the dispatch
    rather than a copy of it: the mapping used to be written twice in the
    script, and the test read whichever copy appeared first in the file.
    That left the copy the dispatch actually uses unchecked -- changing
    it there sent a card to the wrong brief and no test went red.

    The repository to look in has to be named, because the launcher's
    default points at the build machine's clone, which does not exist on
    the machine running the test. Without it every task comes back
    "MISSING" and the test says nothing about the mapping.
    """
    env = dict(os.environ)
    env["LENGSUO_REPO_SRC"] = str(repo_src if repo_src is not None else ROOT)
    if brief_dir is not None:
        env["LENGSUO_L3B_BRIEF_DIR"] = str(brief_dir)
    result = subprocess.run(
        [find_bash(), str(LAUNCHER), "--resolve-brief", *tasks],
        capture_output=True, text=True, check=False, timeout=120, env=env)
    if result.returncode != 0:
        raise AssertionError("the launcher failed to resolve briefs: %s"
                             % result.stderr)
    mapping = {}
    for line in result.stdout.splitlines():
        parts = line.split()
        if len(parts) < 2:
            continue
        task, path = parts[0], parts[1]
        if path == "MISSING":
            continue
        mapping[task] = pathlib.PurePosixPath(path).name
    return mapping


def _task_to_brief():
    """Return {task: brief file name} as the launcher resolves it."""
    return _resolve_briefs(["O", "N", "Q", "R", "M", "I", "J", "K", "L"])


class DispatchBriefTests(unittest.TestCase):
    @_skip_without_docs
    def test_every_round_brief_exists(self):
        mapping = _task_to_brief()
        self.assertTrue(mapping, "no task->brief mapping parsed")
        for task, brief in sorted(mapping.items()):
            with self.subTest(task=task):
                self.assertTrue(
                    (ROOT / "docs" / brief).is_file(),
                    "task %s points at missing brief %s" % (task, brief))

    @_skip_without_docs
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
        reachable = set(mapping.values())
        shipped = {p.name for p in DOCS.glob("cc-task-brief*.md")}
        self.assertEqual(
            shipped - reachable - finished, set(),
            "briefs exist but no task letter points at them; add a case "
            "in launch_l3b_short.sh")

    def test_third_round_tasks_use_the_third_round_brief(self):
        """Guards the specific regression: M/N briefly mapped to round two."""
        mapping = _task_to_brief()
        self.assertEqual(mapping.get("M"), "cc-task-brief-l3d.md")

    def test_fourth_round_task_does_not_fall_back(self):
        """O must not hit the default arm (round two's brief)."""
        mapping = _task_to_brief()
        self.assertIn("O", mapping)
        self.assertEqual(mapping["O"], "cc-task-brief-l3e.md")
        self.assertNotIn(mapping["O"], ("cc-task-brief-l3.md",
                                        "cc-task-brief-l3b.md"))

    def test_fifth_round_tasks_use_the_fifth_round_brief(self):
        """N/Q/R are the fifth round; P/S live in the client repo, not here."""
        mapping = _task_to_brief()
        for task in ("N", "Q", "R"):
            with self.subTest(task=task):
                self.assertEqual(mapping.get(task),
                                 "cc-task-brief-l3f.md")
        self.assertNotIn("P", mapping,
                         "task P belongs to the client repo line")

    def test_n_does_not_point_at_the_third_round_brief(self):
        """N moved to the fifth round; the third round would redo finished work.

        Round three's brief asks for the envelope to be written from
        scratch. That work is already done (18 gtest, commit 2f0ea73), so
        dispatching N against it would burn a card rebuilding it.
        """
        mapping = _task_to_brief()
        self.assertNotEqual(mapping.get("N"), "cc-task-brief-l3d.md")

    @_skip_without_docs
    def test_fifth_round_brief_has_a_section_for_every_mapped_task(self):
        """Each task the script can dispatch needs its own section.

        A brief without the task's section sends the card off to read
        another round's instructions -- the exact failure the mapping
        above is meant to prevent.
        """
        mapping = _task_to_brief()
        text = (DOCS / "cc-task-brief-l3f.md").read_text(encoding="utf-8")
        for task in ("N", "Q", "R"):
            if mapping.get(task) != "cc-task-brief-l3f.md":
                continue
            with self.subTest(task=task):
                self.assertRegex(text, r"(?m)^## 任务 %s[：: ]" % task)

    @_skip_without_docs
    def test_wire_contract_exists(self):
        """P and Q are two repos; the shared contract must live in the repo."""
        contract = ROOT / "docs" / "kernel-wire-contract.md"
        self.assertTrue(contract.is_file())
        text = contract.read_text(encoding="utf-8")
        for needed in ("--lengsuo-kernel-pipe", "Hello", "KeyEnvelope",
                       "0x81", "CanvasSample", "大端"):
            with self.subTest(needed=needed):
                self.assertIn(needed, text)

    @_skip_without_docs
    def test_round_four_brief_names_its_task(self):
        """The brief itself must state the task letter the prompt will ask for."""
        brief = (DOCS / "cc-task-brief-l3e.md").read_text(encoding="utf-8")
        self.assertRegex(brief, r"(?m)^## 任务 O[：: ]")
        self.assertIn("允许改动的文件", brief)
        self.assertIn("report.md", brief)


if __name__ == "__main__":
    unittest.main()
