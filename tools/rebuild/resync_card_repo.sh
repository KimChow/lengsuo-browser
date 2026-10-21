#!/usr/bin/env bash
# Re-sync a card's working copy to the latest main without losing work.
#
# Why this is a separate script
# -----------------------------
# The resync used to live inline in launch_l3b_short.sh, where it was:
#
#     if [ -n "$(git status --porcelain)" ]; then
#         git add -A
#         git commit -m "wip: auto-stash before resync"
#     fi
#     git fetch origin main && git reset --hard origin/main
#
# That "stash" destroyed the work instead of preserving it. The commit it
# just made was never on a branch, so `reset --hard origin/main` threw it
# away -- and untracked files were removed by the reset with no trace at
# all.
#
# Task N's working copy holds its three envelope source files as untracked
# files on purpose: they are the card's *input* (the finished envelope from
# an earlier card), not the card's output. A resync deleted them, and the
# card would have had nothing to merge. Its brief says so explicitly: the
# envelope sources are already in the working copy, use those three files
# directly, do not go looking in another clone.
#
# The fix is `git stash push --include-untracked`, which keeps untracked and
# modified files, survives the reset, and `stash pop` restores them.
#
# Usage: resync_card_repo.sh <repo-dir> [remote-branch]
set -u

REPO="${1:-}"
BRANCH="${2:-main}"

# The check goes through git rather than bash's own test, because the two
# disagree about Windows paths: git understands "D:/dir/repo", while the
# shell's file test only understands its own mount form. Asking git is
# both simpler and the thing that actually matters here.
if [ -z "$REPO" ] || ! git -C "$REPO" rev-parse --git-dir >/dev/null 2>&1; then
    echo "resync: not a git repository: $REPO" >&2
    exit 1
fi

dirty="$(git -C "$REPO" status --porcelain 2>/dev/null)"
if [ -n "$dirty" ]; then
    # --include-untracked is the whole point: the card's input files are
    # untracked by design.
    git -C "$REPO" stash push --include-untracked -q \
        -m "wip before resync" >/dev/null 2>&1
fi

if ! git -C "$REPO" fetch -q origin "$BRANCH"; then
    echo "resync: fetch of origin/$BRANCH failed" >&2
    # Put the work back before giving up; the caller may want to retry.
    git -C "$REPO" stash pop -q >/dev/null 2>&1 || true
    exit 1
fi

git -C "$REPO" reset -q --hard "origin/$BRANCH" || {
    echo "resync: reset to origin/$BRANCH failed" >&2
    git -C "$REPO" stash pop -q >/dev/null 2>&1 || true
    exit 1
}

# Restore the work. A failed pop must not abort the launch: the card can
# still run, it just starts from a clean tree.
git -C "$REPO" stash pop -q >/dev/null 2>&1 || true

echo "resync: $REPO is now at $(git -C "$REPO" rev-parse --short HEAD)"
