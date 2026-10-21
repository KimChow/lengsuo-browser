"""The dispatch resync must not destroy a card's working copy.

Why this exists
---------------
`tools/rebuild/resync_card_repo.sh` re-syncs a card working copy to the
latest main before a session starts. It used to do that inline in
`launch_l3b_short.sh`, like this:

    if [ -n "$(git status --porcelain)" ]; then
        git add -A
        git commit -m "wip: auto-stash before resync"
    fi
    git fetch origin main && git reset --hard origin/main

That "stash" destroyed the work instead of preserving it. The commit it
had just made was never on any branch, so `reset --hard origin/main`
threw it away -- and untracked files were removed by the reset with no
trace. The work was not merely moved; it was gone.

Task N's working copy holds its three envelope source files as untracked
files on purpose: they are the card's *input* (the finished envelope from
an earlier card), not the card's output. A resync deleted them, and the
card would have had nothing to merge. The brief says so explicitly --
"the envelope source files are already in the working copy, use these
three files directly, do not go looking in another clone".

So the resync now stashes with `--include-untracked` and pops afterwards.
This test runs the real script against a scratch repo and asserts the
files survive.

How the test is driven
----------------------
The script under test is bash, and this machine has two git builds with
different ideas about how to spell a Windows path -- one of them reads a
backslash path as an scp-style `host:path` and tries to reach it over
the network. Rather than translate paths between the two, the test lets
bash do all the work: a bash driver creates the scratch repos, invokes
the script, and reports the resulting state. Every path bash touches is
then a path bash made.

Two properties of the driver are load-bearing, and both come from an
accident this test's own first version had:

* `set -e` -- without it a failed `cd` leaves the driver running in
  whatever directory it happened to be in. That is how an earlier
  version of this test ended up running `git add -A` in the real
  repository, and how the script under test ended up pointed at the
  real working tree. A test about not clobbering the repository must
  not be able to clobber it.
* the scratch tree is created outside the repository -- so even a
  mistake cannot leave anything behind in the real tree, and so the
  repository's own `git add` can never pick the scratch repos up.
"""
import pathlib
import subprocess
import sys
import tempfile
import unittest

sys.path.insert(0, str(pathlib.Path(__file__).resolve().parent))
from bash_helper import find_bash  # noqa: E402  (path set up above)

ROOT = pathlib.Path(__file__).resolve().parents[1]
RESYNC = ROOT / "tools" / "rebuild" / "resync_card_repo.sh"
LAUNCHER = ROOT / "tools" / "rebuild" / "launch_l3b_short.sh"

# The driver builds a scratch repo pair, runs the resync script over the
# card, and prints the state of the card afterwards.
#
# The script under test is named relative to the repository root, which
# is the driver's working directory: the caller cannot hand bash a path
# it can open.
DRIVER = r"""
set -eu

# The script under test, anchored while the working directory is still
# the repository root.
SCRIPT=$(pwd)/tools/rebuild/resync_card_repo.sh

# The scratch tree is a plain mktemp directory, outside the repository
# so that the real tree cannot pick it up and so that the file watchers
# on the repository do not slow every run down. It is removed on the
# way out.
T=$(mktemp -d)
T=$(cd "$T" && pwd)
# The removal leaves the working directory first.
#
# On Windows, deleting a directory that is the current directory of a
# running process fails, and `rm` reports nothing: the tree survives and
# the test that checks for leftovers fails on a machine where the code
# under test is fine. The checkout of a scratch repository ends with the
# shell sitting inside it, so the `cd` is what makes the removal work on
# both platforms.
trap 'cd / && command rm -rf "$T"' EXIT

if [ ! -d "$T" ]; then
    echo "RESYNC_RC=125"
    exit 0
fi

git init -q -b main "$T/origin"
cd "$T/origin"
git config user.email t@example.com
git config user.name t
echo base > base.txt
git add -A
git commit -q -m base

git init -q -b main "$T/card"
cd "$T/card"
git config user.email t@example.com
git config user.name t
git remote add origin "$T/origin"
git fetch -q origin
git reset -q --hard origin/main

# The card's pending work: an untracked input file (the envelope source
# files of task N) and an edit to a tracked file.
mkdir -p native
echo "// envelope implementation" > native/l3_envelope.cc
echo "card edit" >> base.txt

# Upstream moved on.
cd "$T/origin"
git commit -q --allow-empty -m upstream

if [ ! -f "$SCRIPT" ]; then
    echo "RESYNC_RC=127"
    exit 0
fi
bash "$SCRIPT" "$T/card" main
echo "RESYNC_RC=$?"
echo "ENVELOPE=$(cat "$T/card/native/l3_envelope.cc" 2>/dev/null)"
echo "BASE=$(cat "$T/card/base.txt" 2>/dev/null | tr '\n' '|')"
echo "HEAD=$(cd "$T/card" && git log --oneline -1 2>/dev/null)"
echo "STATUS=[$(cd "$T/card" && git status --porcelain 2>/dev/null | tr '\n' ';')]"
"""


def _scratch_trees():
    """Return the scratch trees the driver would leave behind.

    A scratch tree is a `mktemp` directory holding the two repositories
    the driver builds: the origin it pushes to and the card it resyncs.
    """
    found = set()
    for path in pathlib.Path(tempfile.gettempdir()).glob("tmp.*"):
        if (path / "origin").is_dir() or (path / "card").is_dir():
            found.add(path.name)
    return found


def _run_driver(driver=DRIVER):
    """Run the bash driver and return (result, parsed output).

    The driver is written to a file and executed, rather than passed to
    `bash -c`. Under a Windows-spawned bash, an inline command string
    has its quoting mangled badly enough that `mktemp` and `mkdir`
    quietly produce nothing -- the driver then runs with an empty path
    and every step after it does the wrong thing. A file is read by
    bash as written.

    The output is captured into a file rather than through a pipe. A
    pipe needs both ends closed before it can be read to the end, and
    the driver runs git, which leaves processes behind holding the
    write end. Under `unittest`, which replaces the process's own
    stdout, that inherited handle is not the one the test reads, and the
    read waits for a process that is not going to close it.
    """
    script = ROOT / "tmp" / "resync_driver.sh"
    out_path = ROOT / "tmp" / "resync_driver.out"
    script.parent.mkdir(parents=True, exist_ok=True)
    # Newlines must be LF: a CRLF file gives bash a stray carriage
    # return at the end of every line, and the driver falls apart.
    script.write_bytes(driver.replace(chr(13) + chr(10), chr(10)).encode(
        "utf-8"))
    try:
        with out_path.open("wb") as sink:
            result = subprocess.run([find_bash(), "tmp/resync_driver.sh"],
                                    stdout=sink, stderr=subprocess.STDOUT,
                                    stdin=subprocess.DEVNULL,
                                    check=False, cwd=str(ROOT))
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


class ResyncCardRepoTest(unittest.TestCase):
    """Drive the real resync script against a scratch repo."""

    # The driver is run once and the result is shared by every test in
    # the class. Each run costs over a minute on this machine -- every
    # command it runs is looked up through a wrapper on PATH, and it
    # drives git a dozen times -- and the assertions are all about the
    # same run, so running it again per test would multiply that cost
    # for no extra information.
    _result = None
    _out = None
    _clean_result = None
    _clean_out = None

    @classmethod
    def setUpClass(cls):
        # The scratch trees that existed before the drivers ran.
        #
        # The test below asks whether the driver leaves anything behind.
        # Answering that with a look at the whole temporary directory
        # would make the result depend on what other runs left there:
        # a tree from an earlier session, or from another program, would
        # be reported as this run's failure. The question is about the
        # difference the run makes, so the difference is what is
        # measured.
        cls._scratch_before = _scratch_trees()
        cls._result, cls._out = _run_driver()
        # The clean-tree case needs a driver without the pending work.
        clean = DRIVER.replace('echo "card edit" >> base.txt\n', "")
        clean = clean.replace(
            'echo "// envelope implementation" > native/l3_envelope.cc\n', "")
        cls._clean_result, cls._clean_out = _run_driver(clean)

    def _run(self):
        return self._result, self._out

    def test_untracked_card_input_survives_the_resync(self):
        """The card's untracked input files must survive the resync."""
        result, out = self._run()
        self.assertEqual(result.returncode, 0, result.stderr)
        self.assertEqual(out.get("RESYNC_RC"), "0", result.stderr)
        self.assertIn("envelope implementation", out.get("ENVELOPE", ""),
                      "the resync deleted the card's untracked input file")

    def test_modified_tracked_file_survives_the_resync(self):
        """A card's in-progress edit to a tracked file must survive too."""
        _, out = self._run()
        self.assertIn("card edit", out.get("BASE", ""),
                      "the resync discarded the card's edit to a tracked file")

    def test_resync_still_moves_the_base_to_the_latest_main(self):
        """Preserving the work must not stop the base from advancing."""
        _, out = self._run()
        self.assertIn("upstream", out.get("HEAD", ""),
                      "the resync did not advance the base to the latest main")

    def test_a_clean_tree_is_left_clean(self):
        """A card with nothing pending must come out clean."""
        result, out = self._clean_result, self._clean_out
        self.assertEqual(result.returncode, 0, result.stderr)
        self.assertEqual(out.get("RESYNC_RC"), "0", result.stderr)
        self.assertEqual(out.get("STATUS"), "[]",
                         "the resync left the tree dirty")

    def test_the_scratch_tree_is_cleaned_up(self):
        """The driver must not leave anything behind in the repository.

        The scratch tree lives under the repository's tmp/ directory, so
        a driver that failed to clean up would leave untracked files in
        the real tree -- and the next `git add -A` would pick them up.

        Only the trees that appeared during this run are considered:
        the temporary directory is shared with everything else on the
        machine, and a tree left by an earlier run says nothing about
        whether this one cleaned up after itself.
        """
        result, out = self._run()
        self.assertEqual(out.get("RESYNC_RC"), "0", out)
        leftovers = sorted(_scratch_trees() - self._scratch_before)
        self.assertEqual(leftovers, [],
                         "the driver left scratch trees behind: %r" % leftovers)


class ResyncSourceTest(unittest.TestCase):
    """Pin the shape of the resync in the shipped scripts."""

    def test_the_old_destructive_pattern_is_gone(self):
        """The old code committed then reset, which discarded the commit."""
        text = LAUNCHER.read_text(encoding="utf-8")
        self.assertNotIn("auto-stash before resync", text,
                         "the launcher still commits the dirty state")
        self.assertNotIn("add -A", text,
                         "the launcher still stages everything before resetting")

    def test_the_resync_uses_a_stash_that_keeps_untracked_files(self):
        text = RESYNC.read_text(encoding="utf-8")
        self.assertIn("stash push --include-untracked", text)
        self.assertIn("stash pop", text)

    def test_the_launcher_calls_the_resync_script(self):
        """The resync must be the tested script, not an inline copy."""
        text = LAUNCHER.read_text(encoding="utf-8")
        self.assertIn("resync_card_repo.sh", text,
                      "the launcher does not use the tested resync script")


if __name__ == "__main__":
    unittest.main()
