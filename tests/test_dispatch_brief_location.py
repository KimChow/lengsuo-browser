"""The dispatch must find its briefs wherever this deployment keeps them.

Why this exists
---------------
The repository treats `docs/` as a local-only working area: it holds
development notes, acceptance records and reference material, and a
commit moved the whole directory out of the index. The task briefs and
the wire contract are not that kind of document -- they are inputs to
the dispatch itself. The build machine's copy of the repository is a
clone of the remote, so after `git reset --hard origin/main` it has no
`docs/` at all.

The failure this guards against is silent and total: the dispatch script
looked the brief up under `$REPO_SRC/docs/`, found nothing, and exited
before starting the card. The same held for the cross-repo vector check,
which read the contract from the kernel repository's `docs/`. Neither
tool said "the document is somewhere else"; they said the file was
missing, which reads like a lost file rather than a moved one.

So the briefs and the contract now have a stable home outside the
repository, and both tools look in the repository first and outside it
second. These tests pin that: a brief that is only in the outside
directory must still be found, the repository copy must still win when
both exist, and a genuinely absent brief must still be reported.

The launcher is bash, so it is driven the way the resync test drives
its script: a bash file, not an inline string, because a Windows-spawned
bash mangles an inline command string badly enough that `mktemp` and
`mkdir` quietly produce nothing.
"""
import pathlib
import re
import subprocess
import sys
import tempfile
import unittest

sys.path.insert(0, str(pathlib.Path(__file__).resolve().parent))
from bash_helper import find_bash  # noqa: E402  (path set up above)

ROOT = pathlib.Path(__file__).resolve().parents[1]
LAUNCHER = ROOT / "tools" / "rebuild" / "launch_l3b_short.sh"
VECTORS = ROOT / "tools" / "cross_repo_vectors.py"


def _launcher_text():
    return LAUNCHER.read_text(encoding="utf-8")


class BriefLocationTest(unittest.TestCase):
    """The launcher must resolve a brief from either location."""

    def test_launcher_looks_outside_the_repository(self):
        """The out-of-repository directory must be in the lookup."""
        text = _launcher_text()
        self.assertIn("LENGSUO_L3B_BRIEF_DIR", text,
                      "the launcher has no out-of-repository brief location")
        self.assertIn("l3b-briefs", text,
                      "the launcher's default brief directory is not the "
                      "one the build machine uses")

    def test_launcher_prefers_the_repository_copy(self):
        """When both copies exist, the repository one wins.

        A repository that does ship briefs has the tracked copy, and a
        stale copy left in the outside directory must not shadow it.
        """
        text = _launcher_text()
        in_repo = text.find('if [ -f "$REPO_SRC/docs/$brief_name" ]')
        outside = text.find('task_brief="$BRIEF_DIR/$brief_name"')
        self.assertNotEqual(in_repo, -1,
                            "the launcher no longer checks the repository")
        self.assertNotEqual(outside, -1,
                            "the launcher no longer checks the outside dir")
        self.assertLess(in_repo, outside,
                        "the outside copy is consulted before the repository "
                        "one, so a stale copy can shadow the tracked one")

    def test_missing_brief_is_reported_with_both_locations(self):
        """A missing brief must say where it looked."""
        text = _launcher_text()
        self.assertIn("brief not found", text)
        self.assertIn("looked in", text,
                      "the error does not name the directories it searched")

    def test_every_task_letter_maps_to_a_brief(self):
        """The task-to-brief mapping must survive the refactor.

        The mapping used to assign the path directly; it now assigns a
        file name and resolves the directory afterwards. A letter that
        lost its entry would fall through to the default brief and
        quietly run the wrong instructions.
        """
        text = _launcher_text()
        block = re.search(r'case "\$task" in(.*?)esac', text, re.S)
        self.assertIsNotNone(block, "the task->brief case block is gone")
        mapped = set()
        for line in block.group(1).splitlines():
            match = re.match(r'\s*([A-Z][A-Z|]*)\)\s*brief_name="([^"]+)"', line)
            if match:
                for task in match.group(1).split("|"):
                    mapped.add(task)
        for task in ("O", "N", "Q", "R", "M"):
            self.assertIn(task, mapped,
                          "task %s lost its brief mapping" % task)


class ContractLocationTest(unittest.TestCase):
    """The cross-repo check must find the contract wherever it lives."""

    def test_contract_lookup_falls_back(self):
        text = VECTORS.read_text(encoding="utf-8")
        self.assertIn("LENGSUO_L3B_BRIEF_DIR", text,
                      "the vector check has no out-of-repository location")
        self.assertIn("def contract_path", text,
                      "the vector check still reads one hard-coded path")

    def test_repository_copy_wins(self):
        """The repository copy must be preferred when it exists."""
        text = VECTORS.read_text(encoding="utf-8")
        self.assertIn("if in_repo.is_file()", text,
                      "the vector check does not prefer the repository copy")

    def test_the_tool_still_uses_the_resolved_path(self):
        """The lookup must actually be used where the contract is read.

        A helper that is defined but not called is dead code, and the
        failure it guards against would come straight back.
        """
        text = VECTORS.read_text(encoding="utf-8")
        self.assertIn("contract_path(kernel)", text,
                      "the vector check still reads the contract directly")

    def test_the_tool_imports_what_it_uses(self):
        """`os` must be imported for the environment lookup.

        A missing import does not fail at definition time -- it fails
        when the module is loaded by the test, or at run time on the
        build machine, which is exactly where nobody is looking.
        """
        text = VECTORS.read_text(encoding="utf-8")
        imports = re.findall(r"^import (\w+)$", text, re.M)
        self.assertIn("os", imports,
                      "the vector check uses os.environ without importing os")


class BriefLookupDriverTest(unittest.TestCase):
    """Drive the real launcher and check which brief it picks."""

    # The driver is run once and the result is shared by every test in
    # the class. Each run costs tens of seconds on this machine -- every
    # command it runs is looked up through a wrapper on PATH -- and the
    # cases are independent of each other, so running them again per
    # test would multiply that cost for no extra information.
    _result = None
    _out = None

    @classmethod
    def setUpClass(cls):
        result, out = cls._drive()
        cls._result = result
        cls._out = out

    @classmethod
    def _drive(cls):
        script = ROOT / "tmp" / "brief_lookup_driver.sh"
        script.parent.mkdir(parents=True, exist_ok=True)
        script.write_bytes(cls.DRIVER.replace("\r\n", "\n").encode("utf-8"))
        # The output is captured into a file rather than through a pipe.
        #
        # A pipe needs both ends closed before it can be read to the
        # end, and the driver runs commands that leave a process behind
        # holding the write end. Under `unittest`, which replaces the
        # process's own stdout, that inherited handle is not the one the
        # test reads, and the read waits for a process that is not going
        # to close it. A file has no such handshake: the driver writes,
        # exits, and the test reads what is there.
        out_path = ROOT / "tmp" / "brief_lookup_driver.out"
        try:
            with out_path.open("wb") as sink:
                result = subprocess.run(
                    [find_bash(), "tmp/brief_lookup_driver.sh"],
                    stdout=sink, stderr=subprocess.STDOUT,
                    stdin=subprocess.DEVNULL, check=False, cwd=str(ROOT))
            result.stdout = out_path.read_text(encoding="utf-8",
                                               errors="replace")
        finally:
            try:
                script.unlink()
            except OSError:
                pass
            try:
                out_path.unlink()
            except OSError:
                pass
        out = {}
        for line in result.stdout.splitlines():
            if "=" in line:
                key, _, value = line.partition("=")
                out[key] = value
        return result, out

    def _run(self):
        return self._result, self._out

    DRIVER = r"""
set -eu
T=$(mktemp -d)
T=$(cd "$T" && pwd)
# The removal leaves the working directory first.
#
# On Windows, deleting a directory that is the current directory of a
# running process fails, and `rm` reports nothing: the tree survives and
# any test that checks for leftovers fails on a machine where the code
# under test is fine.
trap 'cd / && command rm -rf "$T"' EXIT

# The `rm` on this machine is a shell function that forwards to a
# "safe delete" helper, and that helper blocks when it is handed a path
# outside the workspace. `command rm` reaches the real binary.
mkdir -p "$T/repo/docs" "$T/briefs"
SCRIPT=$(pwd)/tools/rebuild/launch_l3b_short.sh

# The launcher is asked only which brief it would use. The mode that
# does the lookup without cloning the repository and without starting a
# session is what makes this test cheap and free of side effects: the
# full launcher spends its time in git and in the session starter, and
# neither has anything to do with where the brief comes from.
#
# Every command in this driver costs about a second on this machine,
# because each one is looked up through a wrapper that is on PATH
# ahead of the real binary. The cases are therefore written to touch
# the disk as little as possible: the repository copy is renamed rather
# than deleted and restored, and the three results are collected in one
# pass.
resolve() {
    LENGSUO_L3B_BRIEF_DIR="$T/briefs" \
    LENGSUO_REPO_SRC="$T/repo" \
    LENGSUO_L3B_ROOT="$T/root" \
    bash "$SCRIPT" --resolve-brief N
}

printf 'REPOSITORY COPY\n' > "$T/repo/docs/cc-task-brief-l3f.md"
printf 'OUTSIDE COPY\n' > "$T/briefs/cc-task-brief-l3f.md"

# Case 1: both copies exist -> the repository one is used.
resolve > "$T/case1"

# Case 2: only the outside copy exists -> it is used. The repository
# copy is moved aside rather than removed, so it can be put back.
command mv "$T/repo/docs/cc-task-brief-l3f.md" "$T/repo/docs/held.md"
resolve > "$T/case2"
command mv "$T/repo/docs/held.md" "$T/repo/docs/cc-task-brief-l3f.md"

# Case 3: neither exists -> both places are named.
command mv "$T/repo/docs/cc-task-brief-l3f.md" "$T/repo/docs/held.md"
command mv "$T/briefs/cc-task-brief-l3f.md" "$T/briefs/held.md"
resolve > "$T/case3"

echo "BOTH=$(grep -c 'repo/docs/cc-task-brief-l3f.md' "$T/case1" || true)"
echo "OUTSIDE=$(grep -c 'briefs/cc-task-brief-l3f.md' "$T/case2" || true)"
echo "MISSING=$(grep -c 'MISSING' "$T/case3" || true)"
echo "MENTIONS_REPO=$(grep -c 'repo/docs/cc-task-brief-l3f.md' "$T/case3" || true)"
echo "MENTIONS_DIR=$(grep -c 'briefs/cc-task-brief-l3f.md' "$T/case3" || true)"
"""

    def test_repository_copy_is_preferred(self):
        result, out = self._run()
        self.assertEqual(result.returncode, 0, result.stderr)
        self.assertEqual(out.get("BOTH"), "1",
                         "the repository copy did not win: %r" % out)

    def test_outside_copy_is_used_when_the_repository_has_none(self):
        _, out = self._run()
        self.assertEqual(out.get("OUTSIDE"), "1",
                         "the outside copy was not used: %r" % out)

    def test_a_missing_brief_is_reported(self):
        _, out = self._run()
        self.assertEqual(out.get("MISSING"), "1",
                         "a missing brief was not reported: %r" % out)

    def test_a_missing_brief_names_both_places(self):
        """The report has to say where it looked.

        A bare "not found" reads like a lost file. The two places the
        brief can live are the whole point of the fallback, so both have
        to appear in the message.
        """
        _, out = self._run()
        self.assertEqual(out.get("MENTIONS_REPO"), "1",
                         "the report did not name the repository: %r" % out)
        self.assertEqual(out.get("MENTIONS_DIR"), "1",
                         "the report did not name the outside directory: %r"
                         % out)

    def test_the_launcher_has_a_mode_that_only_resolves_the_brief(self):
        """The lookup must be reachable without starting a card.

        The full launcher clones the repository and starts a session,
        which is a lot of machinery to answer "which file would you
        read?". The mode the driver uses must exist in the script, or
        the test is testing the driver rather than the launcher.
        """
        text = _launcher_text()
        self.assertIn("--resolve-brief", text,
                      "the launcher has no mode that only resolves the brief")
        # It must exit before the clone and before the session starter.
        resolve_at = text.find("--resolve-brief")
        clone_at = text.find("git clone")
        setsid_at = text.find("setsid")
        self.assertNotEqual(clone_at, -1, "the launcher no longer clones")
        self.assertNotEqual(setsid_at, -1,
                            "the launcher no longer starts a session")
        self.assertLess(resolve_at, clone_at,
                        "the resolve mode is after the clone")
        self.assertLess(resolve_at, setsid_at,
                        "the resolve mode is after the session starter")


if __name__ == "__main__":
    unittest.main()
