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
This test runs the real script against a scratch clone and asserts the
files survive.
"""
import pathlib
import shutil
import subprocess
import tempfile
import unittest

ROOT = pathlib.Path(__file__).resolve().parents[1]
RESYNC = ROOT / "tools" / "rebuild" / "resync_card_repo.sh"
LAUNCHER = ROOT / "tools" / "rebuild" / "launch_l3b_short.sh"


def _git(repo, *args):
    return subprocess.run(["git", "-C", str(repo)] + list(args),
                          capture_output=True, text=True, check=False)


def _relative_to_root(path):
    """Return `path` relative to the repository root, in POSIX form.

    The resync script is invoked with the working directory set to the
    repository root. A relative path is what bash can resolve here: on
    this machine bash's git and native git disagree about how to spell an
    absolute Windows path, and bash eats the backslashes. On the build
    host the absolute Linux path is used instead; the script does not
    care which it gets.
    """
    return str(pathlib.Path(path).resolve().relative_to(ROOT)).replace(
        "\\", "/")


class ResyncCardRepoTest(unittest.TestCase):
    """Drive the real resync script against a scratch clone."""

    def setUp(self):
        # The scratch tree lives inside the repository so the script can be
        # invoked with a path relative to the root.
        self.tmp = pathlib.Path(tempfile.mkdtemp(dir=str(ROOT)))
        self.addCleanup(shutil.rmtree, self.tmp, ignore_errors=True)
        self.origin = self.tmp / "origin"
        self.card = self.tmp / "card"

        self.origin.mkdir()
        _git(self.origin, "init", "-q", "-b", "main")
        _git(self.origin, "config", "user.email", "t@example.com")
        _git(self.origin, "config", "user.name", "t")
        (self.origin / "base.txt").write_text("base\n", encoding="utf-8")
        _git(self.origin, "add", "-A")
        _git(self.origin, "commit", "-q", "-m", "base")

        _git(self.tmp, "clone", "-q", str(self.origin), str(self.card))
        _git(self.card, "config", "user.email", "t@example.com")
        _git(self.card, "config", "user.name", "t")

    def _resync(self, repo=None):
        return subprocess.run(
            ["bash", "tools/rebuild/resync_card_repo.sh",
             _relative_to_root(repo or self.card), "main"],
            capture_output=True, text=True, check=False, cwd=str(ROOT))

    def test_untracked_card_input_survives_the_resync(self):
        """The card's untracked input files must survive the resync."""
        envelope = self.card / "native" / "l3_envelope.cc"
        envelope.parent.mkdir(parents=True, exist_ok=True)
        envelope.write_text("// envelope implementation\n", encoding="utf-8")

        # Upstream moved on since the clone.
        _git(self.origin, "commit", "-q", "--allow-empty", "-m", "upstream")

        result = self._resync()
        self.assertEqual(result.returncode, 0, result.stderr)
        self.assertTrue(envelope.is_file(),
                        "the resync deleted the card's untracked input file")
        self.assertIn("envelope implementation",
                      envelope.read_text(encoding="utf-8"))

    def test_modified_tracked_file_survives_the_resync(self):
        """A card's in-progress edit to a tracked file must survive too."""
        tracked = self.card / "base.txt"
        tracked.write_text("base\ncard edit\n", encoding="utf-8")
        self._resync()
        self.assertIn("card edit", tracked.read_text(encoding="utf-8"))

    def test_resync_still_moves_the_base_to_the_latest_main(self):
        """Preserving the work must not stop the base from advancing."""
        _git(self.origin, "commit", "-q", "--allow-empty", "-m", "upstream")
        envelope = self.card / "native" / "l3_envelope.cc"
        envelope.parent.mkdir(parents=True, exist_ok=True)
        envelope.write_text("// envelope\n", encoding="utf-8")

        self._resync()
        head = _git(self.card, "log", "--oneline", "-1").stdout
        self.assertIn("upstream", head,
                      "the resync did not advance the base to the latest main")

    def test_a_clean_tree_is_left_clean(self):
        """A card with nothing pending must come out clean."""
        result = self._resync()
        self.assertEqual(result.returncode, 0, result.stderr)
        self.assertEqual(_git(self.card, "status", "--porcelain").stdout, "")

    def test_a_non_repository_is_rejected(self):
        """The script must fail loudly rather than silently do nothing."""
        plain = self.tmp / "plain"
        plain.mkdir()
        result = self._resync(repo=plain)
        self.assertNotEqual(result.returncode, 0)


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
