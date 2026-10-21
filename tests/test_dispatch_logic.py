"""The dispatch must not start a card that is already running.

Why this exists
---------------
Two sessions on one working copy do not queue. They edit the same files
at the same time, and whichever finishes second overwrites the other's
work -- so the card that was dispatched first can end up contributing
nothing, and nobody notices because the card did produce a report.

The client line has a guard for this: its card guard refuses a task
whose state is busy. The kernel line had none. A re-dispatch was one
command away, and the working copies of the cards are directories that
persist between dispatches, so the second session would find the first
one's work already there.

The launcher also has to check that the brief it picked actually has a
section for the task. The mapping says which file a task reads; the
file says what the task is. A brief whose section was renamed or
removed sends the card off to read another round's instructions, and
the card does not notice -- it reads a plausible document and
implements the wrong thing. Refusing to dispatch turns that into a
clear message.

The tests drive the real launcher through the mode that only resolves
briefs, and read the dispatch rules from the script's own text. They do
not start sessions: a test that launched a card would leave a process
behind and a working copy to clean up.
"""
import os
import pathlib
import re
import subprocess
import sys
import unittest

sys.path.insert(0, str(pathlib.Path(__file__).resolve().parent))
from bash_helper import find_bash  # noqa: E402  (path set up above)
from docs_helper import doc_path  # noqa: E402  (path set up above)

ROOT = pathlib.Path(__file__).resolve().parents[1]
LAUNCHER = ROOT / "tools" / "rebuild" / "launch_l3b_short.sh"


def _launcher_text():
    return LAUNCHER.read_text(encoding="utf-8")


def _run_resolve(tasks, repo_src=None, brief_dir=None):
    """Run the launcher's resolve mode and return its output.

    The repository to look in is named explicitly: the launcher's
    default points at the build machine's clone, which does not exist on
    the machine running the test, so without it every task comes back
    "MISSING" and the test says nothing.
    """
    env = dict(os.environ)
    env["LENGSUO_REPO_SRC"] = str(repo_src if repo_src is not None else ROOT)
    if brief_dir is not None:
        env["LENGSUO_L3B_BRIEF_DIR"] = str(brief_dir)
    result = subprocess.run(
        [find_bash(), str(LAUNCHER), "--resolve-brief", *tasks],
        capture_output=True, text=True, check=False, timeout=120, env=env)
    return result


class AlreadyRunningTest(unittest.TestCase):
    """A card with a live session must not be dispatched again."""

    def test_the_launcher_checks_for_a_live_session(self):
        """The check has to be in the dispatch path, not only in --status.

        The launcher can report whether a card is alive in its status
        mode, and that check existed long before the dispatch path
        learned to use one. A dispatch that does not look can start a
        second session over a running card.
        """
        text = _launcher_text()
        # The dispatch path is everything after the mode dispatch.
        body = text.split("TASKS=", 1)
        self.assertEqual(len(body), 2,
                         "the launcher lost its dispatch section")
        dispatch = body[1]
        self.assertIn("session.pid", dispatch,
                      "the dispatch path never looks at the card's pid")
        self.assertIn("kill -0", dispatch,
                      "the dispatch path does not test whether the pid is "
                      "alive")

    def test_a_live_card_is_refused(self):
        """The refusal has to happen before the session is started.

        The session is started by `setsid` near the end of the loop. A
        check placed after it would let the second session run, which is
        the thing being prevented.
        """
        text = _launcher_text()
        dispatch = text.split("TASKS=", 1)[1]
        check_at = dispatch.find("kill -0")
        start_at = dispatch.find("setsid")
        self.assertNotEqual(check_at, -1, "no liveness check in the dispatch")
        self.assertNotEqual(start_at, -1, "the launcher no longer starts a "
                                              "session")
        self.assertLess(check_at, start_at,
                        "the liveness check runs after the session is "
                        "started, so the second session is already running")

    def test_the_refusal_names_the_card(self):
        """The message has to say which card and which pid.

        A refusal that does not say what was refused leaves the reader
        guessing which of the dispatched tasks was skipped.
        """
        text = _launcher_text()
        self.assertRegex(text, r"already running",
                         "the refusal does not say the card is running")
        self.assertIn("pid=", text,
                      "the refusal does not report the running pid")


class BriefSectionTest(unittest.TestCase):
    """The brief has to have a section for the task being dispatched."""

    def test_the_dispatch_checks_the_brief_for_the_task_section(self):
        """The section check has to be in the dispatch path."""
        text = _launcher_text()
        dispatch = text.split("TASKS=", 1)[1]
        self.assertRegex(dispatch, r"grep -qE",
                         "the dispatch does not look for the task's section "
                         "in the brief")

    def test_the_check_asks_for_the_task_by_letter(self):
        """The pattern has to name the task, not just any heading.

        A check that accepts any heading passes for every brief, so it
        catches nothing. The heading form used by the briefs is
        `## 任务 X` followed by a colon.
        """
        text = _launcher_text()
        dispatch = text.split("TASKS=", 1)[1]
        match = re.search(r'grep -qE\s+"([^"]+)"', dispatch)
        self.assertIsNotNone(match,
                             "the dispatch does not grep the brief for a "
                             "section")
        pattern = match.group(1)
        self.assertIn("$task", pattern,
                      "the grep pattern does not name the task being "
                      "dispatched")

    def test_the_check_runs_before_the_session_starts(self):
        """A card must not start when its brief has no section for it."""
        text = _launcher_text()
        dispatch = text.split("TASKS=", 1)[1]
        grep_at = dispatch.find("grep -qE")
        start_at = dispatch.find("setsid")
        self.assertNotEqual(grep_at, -1, "no section check in the dispatch")
        self.assertLess(grep_at, start_at,
                        "the section check runs after the session starts")


class SingleMappingTest(unittest.TestCase):
    """The task -> brief mapping must exist in exactly one place.

    It used to be written twice: once for the mode that only resolves
    briefs, and once for the dispatch itself. A test that read the
    source could only find one of them, and it found the copy that was
    not the one the dispatch used. Changing the dispatch's copy sent a
    card to the wrong brief and no test went red.
    """

    def test_the_mapping_is_written_once(self):
        """There must be a single mapping of task letters to briefs.

        The mapping lives in a function now, so the check is that the
        function's case block is the only place a task letter is
        mapped to a brief name.

        Both spellings of the assignment count. A copy that assigns the
        name to a variable and a copy that echoes it are the same rule
        written twice, and only one of them needs to be found for the
        duplication to exist.
        """
        text = _launcher_text()
        blocks = list(re.finditer(
            r'case\s+"\$\w+"\s+in(.*?)esac', text, re.S))
        mapping_blocks = [
            m for m in blocks
            if re.search(r'\)\s*(?:echo\s+)?"?\$?\{?brief', m.group(1))
            or re.search(r'\)\s*(?:echo\s+)?"cc-task-brief', m.group(1))]
        self.assertEqual(len(mapping_blocks), 1,
                         "the task->brief mapping is written %d times; the "
                         "dispatch and the resolve mode can disagree"
                         % len(mapping_blocks))

    def test_both_the_resolve_mode_and_the_dispatch_use_it(self):
        """Both callers must go through the shared function.

        A single block is only a single rule if both paths read it. One
        of them calling something else reintroduces the disagreement the
        single block was meant to remove.
        """
        text = _launcher_text()
        self.assertRegex(text, r"brief_for_task\(\)\s*\{",
                         "there is no shared mapping function")
        # The function is called from the resolve mode and from the
        # dispatch. The dispatch asks for a path, the resolve mode asks
        # for a name; both go through the function that holds the rule.
        self.assertGreaterEqual(text.count("brief_for_task"), 3,
                                "the shared mapping function is not used by "
                                "both the resolve mode and the dispatch")

    def test_the_resolve_mode_answers_from_the_same_function(self):
        """The resolve mode must not re-implement the lookup.

        The resolve mode is what the tests read, so a copy of the rule
        there is a copy that can drift from the one the dispatch uses.
        """
        text = _launcher_text()
        # The mode's own branch, not the first mention of its name: the
        # name also appears in the comment above the script's functions.
        marker = "\n--resolve-brief)"
        self.assertIn(marker, text,
                      "the launcher has no --resolve-brief branch")
        resolve = text.split(marker, 1)[1].split("\n    ;;", 1)[0]
        self.assertNotRegex(resolve, r'case\s+"\$\w+"\s+in',
                            "the resolve mode has its own copy of the "
                            "mapping")
        # It has to ask the shared function instead.
        self.assertIn("brief_path_for_task", resolve,
                      "the resolve mode does not ask the shared lookup")


class ResolveModeTest(unittest.TestCase):
    """The resolve mode must answer for every task letter."""

    def test_every_task_resolves_to_a_brief(self):
        """A task that resolves to nothing cannot be dispatched."""
        result = _run_resolve(["O", "N", "Q", "R", "M", "I", "J"])
        self.assertEqual(result.returncode, 0, result.stderr)
        missing = [line for line in result.stdout.splitlines()
                   if " MISSING " in line]
        self.assertEqual(missing, [],
                         "these tasks have no brief: %r" % missing)

    def test_a_task_letter_with_no_brief_is_reported(self):
        """A brief that cannot be found must be reported, not guessed.

        The resolve mode has to say which file it looked for and where,
        because the two places it looks are both legitimate and a
        reader who is told only "not found" cannot tell which one is
        missing.
        """
        import tempfile
        with tempfile.TemporaryDirectory() as empty:
            # A repository that holds no briefs at all, so neither
            # location has the file.
            result = _run_resolve(["N"], repo_src=empty, brief_dir=empty)
            self.assertIn("MISSING", result.stdout,
                          "a missing brief was not reported: %r"
                          % result.stdout)
            # Both places have to be named: one is the repository, the
            # other the shared directory. The report names them as the
            # two paths it would have read.
            self.assertIn("docs", result.stdout,
                          "the report does not name the repository location")
            self.assertIn("cc-task-brief-l3f.md", result.stdout,
                          "the report does not name the file it wanted")
            # The two paths are distinct, so a reader can tell which
            # one to put the file in.
            parts = result.stdout.split()
            self.assertGreaterEqual(len(parts), 4,
                                    "the report does not give two paths")


class CrossRepoCardTest(unittest.TestCase):
    """A card that touches both repos must be dispatchable end to end.

    Why this exists
    ---------------
    The machine-code alignment card is the first card that has to touch
    the kernel repo and the client repo at once. Two things have to line
    up for it: the launcher has to know which brief the letter points
    at, and that brief has to carry a section for the letter. If either
    is missing the card is refused at dispatch time -- and the refusal
    looks like a script bug rather than a missing section, so the card
    simply never runs.

    The letter is "M0" rather than "M" because "M" is already taken by
    an earlier round with a different brief, and a card that reads the
    wrong brief implements the wrong thing.
    """

    def test_the_cross_repo_card_resolves_to_its_brief(self):
        """The letter must be in the mapping, or it falls back to
        another round's brief."""
        result = _run_resolve(["M0"])
        self.assertEqual(result.returncode, 0, result.stderr)
        self.assertIn("cc-task-brief-l3f.md", result.stdout,
                      "M0 does not resolve to the fifth-round brief: %r"
                      % result.stdout)
        self.assertNotIn("cc-task-brief-l3b.md", result.stdout,
                         "M0 fell back to another round's brief: %r"
                         % result.stdout)

    def test_the_brief_carries_a_section_for_the_card(self):
        """The dispatch refuses a letter the brief has no section for.

        This is the check the launcher itself makes before it starts a
        session, so the brief has to satisfy the same pattern here.

        The brief is looked up the way the launcher looks it up: the
        repository copy first, the shared out-of-repository copy second.
        The repository does not ship `docs/`, so on the build machine
        only the second location has the file -- reading the repository
        path directly would report a missing brief where there is one.
        """
        brief = doc_path("cc-task-brief-l3f.md")
        self.assertIsNotNone(brief,
                             "the fifth-round brief is missing: %s" % brief)
        text = brief.read_text(encoding="utf-8")
        self.assertRegex(text, r"(?m)^## 任务 M0[：: ]",
                         "the brief has no section for the M0 card")

    def test_the_card_may_not_carry_a_chosen_route(self):
        """The card must not decide which repo changes.

        The route is the principal's call: it decides which repo owns
        the machine-code rules. A brief that already picks one turns a
        decision into a fait accompli, and the other route's work never
        happens. The brief has to state the two routes and leave the
        choice open.
        """
        brief = doc_path("cc-task-brief-l3f.md")
        self.assertIsNotNone(brief,
                             "the fifth-round brief is missing: %s" % brief)
        text = brief.read_text(encoding="utf-8")
        start = text.index("## 任务 M0")
        section = text[start:]
        end = section.find("\n## ", 1)
        if end != -1:
            section = section[:end]
        self.assertIn("4A", section,
                      "the M0 section does not name the first route")
        self.assertIn("4B", section,
                      "the M0 section does not name the second route")
        self.assertRegex(section, r"不许自己选|不许自行拍板|未拍板则本卡不启动",
                         "the M0 section does not forbid choosing a route")


class SessionPathTest(unittest.TestCase):
    """The session has to find the agent binary it starts.

    Why this exists
    ---------------
    The card is started with `setsid -f bash run.sh`. That shell is
    neither a login nor an interactive shell, so it reads no profile
    and takes its PATH from the environment the dispatcher happened to
    have. On the build machine the agent binary lives in the user's own
    bin directory, which is not on that PATH -- so the session died
    immediately with "claude: No such file or directory", and the
    dispatcher still reported "started".

    The card therefore has to carry its own PATH. The client line's
    runner does the same thing, and the failure mode is the reason: a
    card that cannot start leaves a worktree and a state file behind
    and looks like it was dispatched.
    """

    def test_the_runner_script_sets_a_path(self):
        """The run script must put the agent's directory on the PATH."""
        text = _launcher_text()
        run = re.search(r"cat > \"\$work/run\.sh\" <<RUNNER\n(.*?)\nRUNNER",
                        text, re.S)
        self.assertIsNotNone(run, "the launcher no longer writes run.sh")
        script = run.group(1)
        self.assertRegex(script, r"PATH=",
                         "the run script does not set a PATH")

    def test_the_path_includes_the_agent_directory(self):
        """The agent binary lives in the user's own bin directory.

        The run script names the variable; the variable carries the
        directory. Both halves are checked: a script that sets PATH to
        something else, or a variable that forgets the directory, both
        leave the session without the binary.
        """
        text = _launcher_text()
        run = re.search(r"cat > \"\$work/run\.sh\" <<RUNNER\n(.*?)\nRUNNER",
                        text, re.S)
        self.assertIsNotNone(run, "the launcher no longer writes run.sh")
        script = run.group(1)
        match = re.search(r'PATH="\$\{?([A-Z_][A-Z0-9_]*)', script)
        self.assertIsNotNone(match,
                             "the run script does not name the variable "
                             "that holds the PATH")
        variable = match.group(1)
        definition = re.search(
            r'^%s="\$\{[^}]*:-([^"]*)\}"' % variable, text, re.M)
        self.assertIsNotNone(definition,
                             "the PATH variable %s has no default" % variable)
        self.assertIn(".local/bin", definition.group(1),
                      "the default PATH does not include the directory "
                      "the agent binary lives in")

    def test_the_path_is_set_for_the_agent_invocation(self):
        """The PATH has to be in effect when the agent is started.

        Setting PATH in a separate line and then starting the agent
        through `env` without it would leave the agent on the old PATH,
        so the check has to be that the same command carries it.
        """
        text = _launcher_text()
        run = re.search(r"cat > \"\$work/run\.sh\" <<RUNNER\n(.*?)\nRUNNER",
                        text, re.S)
        self.assertIsNotNone(run, "the launcher no longer writes run.sh")
        script = run.group(1)
        self.assertRegex(script, r"env\s+[^\n]*PATH=",
                         "the agent is started without the PATH in its "
                         "own environment")


class SessionTurnBudgetTest(unittest.TestCase):
    """A card has to be stopped before it burns its whole budget on research.

    Why this exists
    ---------------
    The client line's runner passes ``--max-turns`` to the agent. The
    kernel line did not, so a kernel card could read files for hours
    without ever writing a line of code.

    That is not hypothetical: the client line's P3 card spent all 60
    turns testing two dependency routes and landed **no code at all**,
    and the kernel line's R card ran for over an hour with an empty
    working tree. The card guard rejects an empty change set, so the
    whole card was wasted.

    The budget is not a deadline: the agent may stop earlier on its own.
    It is a ceiling, and it has to be high enough that a card that is
    genuinely writing code is never cut off mid-file.
    """

    def test_the_agent_is_given_a_turn_limit(self):
        """The run script must cap how many turns the agent may take."""
        text = _launcher_text()
        run = re.search(r"cat > \"\$work/run\.sh\" <<RUNNER\n(.*?)\nRUNNER",
                        text, re.S)
        self.assertIsNotNone(run, "the launcher no longer writes run.sh")
        script = run.group(1)
        self.assertRegex(script, r"--max-turns",
                         "the agent is started without a turn limit")

    def test_the_turn_limit_comes_from_a_named_variable(self):
        """The number must be settable from outside the script.

        A literal in the middle of the run script would mean changing
        the launcher to change the budget. The client line reads it from
        the card's own task spec, so one card can be given a larger
        budget without touching the other.
        """
        text = _launcher_text()
        run = re.search(r"cat > \"\$work/run\.sh\" <<RUNNER\n(.*?)\nRUNNER",
                        text, re.S)
        self.assertIsNotNone(run, "the launcher no longer writes run.sh")
        script = run.group(1)
        match = re.search(r"--max-turns[\" ]+\"?\$?\{?([A-Za-z_][A-Z0-9_]*)",
                          script)
        self.assertIsNotNone(match,
                             "the turn limit is a literal, not a variable")
        variable = match.group(1)
        definition = re.search(
            r'^%s="\$\{[^}]*:-([0-9]+)\}"' % variable, text, re.M)
        self.assertIsNotNone(
            definition,
            "the turn limit variable has no numeric default")

    def test_the_default_budget_is_large_enough_for_a_real_card(self):
        """The ceiling must not cut a card off in the middle of its work.

        A card that writes code, compiles it and runs the tests needs
        room for all three. The limit exists to stop an endless reading
        loop, not to interrupt real work.
        """
        text = _launcher_text()
        run = re.search(r"cat > \"\$work/run\.sh\" <<RUNNER\n(.*?)\nRUNNER",
                        text, re.S)
        self.assertIsNotNone(run, "the launcher no longer writes run.sh")
        script = run.group(1)
        match = re.search(r"--max-turns[\" ]+\"?\$?\{?([A-Za-z_][A-Z0-9_]*)",
                          script)
        self.assertIsNotNone(match,
                             "the turn limit is a literal, not a variable")
        variable = match.group(1)
        definition = re.search(
            r'^%s="\$\{[^}]*:-([0-9]+)\}"' % variable, text, re.M)
        self.assertIsNotNone(definition,
                             "the turn limit variable has no default")
        self.assertGreaterEqual(
            int(definition.group(1)), 150,
            "the default turn budget is too small for a card that also "
            "compiles and tests its work")


if __name__ == "__main__":
    unittest.main()
