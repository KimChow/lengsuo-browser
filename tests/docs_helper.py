"""Find the repository's documents wherever this deployment keeps them.

Why this exists
---------------
The repository treats `docs/` as a local-only working area: it holds
development notes, acceptance records and reference material, and a
commit moved the whole directory out of the index. The build machine's
copy of the repository is a clone of the remote, so after
`git reset --hard origin/main` it has no `docs/` at all.

Several documents are not that kind of document, though. The wire
contract and the task briefs are inputs to the work itself -- the
contract is the single agreement between two repositories, and a brief
is the instructions a card runs on. Losing them takes the machinery
apart: the dispatch script could not find a brief, and the checks that
read the contract reported a missing file, which reads like a lost file
rather than a moved one.

So those documents are kept outside the repository in a stable
directory on the build machine, and the tools that read them look in
the repository first and outside it second.

What is not here
----------------
Only the documents that are inputs to the machinery get a fallback. A
document that records what this repository does -- a design note, a
status line -- has no outside copy, and pretending otherwise would hide
a genuine loss. A caller that wants such a document asks for it by
name through `doc_path` and gets nothing when it is absent; the
caller decides whether that is a skip or a failure.
"""
import os
import pathlib

ROOT = pathlib.Path(__file__).resolve().parents[1]

# Where the shared documents live when the repository does not ship
# them. The override exists so a test can point the lookup at a
# directory of its own without touching the machine's layout.
DOCS_DIR = os.environ.get("LENGSUO_L3B_BRIEF_DIR", "/home/ubuntu/l3b-briefs")


def doc_path(name, root=None):
    """Return the path of a document, or None when it cannot be found.

    The name is a path relative to the repository's `docs/` directory,
    which is how the documents are referred to everywhere else. A name
    with a directory in it (`task-briefs/client-p1-...txt`) is looked
    up under the outside directory as well, because the layout there
    mirrors the one inside.
    """
    base = pathlib.Path(root) if root is not None else ROOT
    in_repo = base / "docs" / name
    if in_repo.is_file():
        return in_repo
    outside = pathlib.Path(DOCS_DIR) / name
    if outside.is_file():
        return outside
    return None


def read_doc(name, root=None):
    """Return the text of a document, or None when it cannot be found.

    Reading is what most callers want; a caller that needs to report
    the absence differently can use `doc_path` and say so itself.
    """
    path = doc_path(name, root=root)
    if path is None:
        return None
    return path.read_text(encoding="utf-8")


if __name__ == "__main__":
    import sys

    for arg in sys.argv[1:]:
        found = doc_path(arg)
        print(arg, "->", found if found else "MISSING")
