#!/bin/bash
# Launch L3 wiring tasks with a self-contained short prompt.
#
# Why not reuse the previous script verbatim: an 11k-char brief passed inline as
# the prompt made two sessions hang for 14 minutes without producing a token.
# A minimal prompt to the same gateway answered in seconds, so the prompt is
# kept short here and the agent reads the (long) brief from disk instead.
#
# Usage:
#   bash launch_l3b_short.sh I J     # explicit tasks
#   bash launch_l3b_short.sh --status
set -u

GATEWAY="${LENGSUO_GATEWAY:-http://192.168.1.151:18080}"
MODEL="${LENGSUO_CC_MODEL:-auto}"
ROOT="${LENGSUO_L3B_ROOT:-/home/ubuntu/l3b-tasks}"
REPO_SRC="${LENGSUO_REPO_SRC:-/home/ubuntu/lengsuo-browser-dev/recipe}"
# The brief is selected by the task name: the third round (M/N) has its own
# brief, the second round (I/J/K/L) keeps the original one.
BRIEF="${LENGSUO_L3B_BRIEF:-}"
# Where the task briefs live when they are not in the repository.
#
# The repository keeps `docs/` as a local-only working area (it holds
# development notes, acceptance records and reference material), so a
# fresh clone of the recipe on the build machine has no `docs/` at all.
# The briefs and the wire contract are not that kind of document: they are
# inputs to the dispatch itself, so they are kept outside the repository
# in a stable directory that survives `git reset --hard`.
#
# Lookup order: the repository copy first (it is the tracked one when a
# repository does ship briefs), then the out-of-repository directory.
BRIEF_DIR="${LENGSUO_L3B_BRIEF_DIR:-/home/ubuntu/l3b-briefs}"

# The task -> brief mapping, in one place.
#
# It used to be written twice -- once for --resolve-brief and once for the
# dispatch itself -- and the test read whichever copy came first in the
# file. That left the copy the dispatch actually uses unchecked: changing
# it there sent a card to the wrong brief, and no test went red. Two
# copies of the same rule is one copy too many.
#
# The mapping lives in its own function so that every caller gets the
# same answer, and so that a test can ask the script what it would do
# instead of reading its source and guessing.
brief_for_task() {
    case "$1" in
        O)   echo "cc-task-brief-l3e.md" ;;
        N|Q|R) echo "cc-task-brief-l3f.md" ;;
        M)   echo "cc-task-brief-l3d.md" ;;
        *)   echo "cc-task-brief-l3b.md" ;;
    esac
}

# The path a task's brief is read from, or nothing if it is not there.
#
# The repository copy wins: a repository that ships briefs has the
# tracked one, and a stale copy left in the outside directory must not
# shadow it.
brief_path_for_task() {
    _name=$(brief_for_task "$1")
    if [ -f "$REPO_SRC/docs/$_name" ]; then
        printf "%s\n" "$REPO_SRC/docs/$_name"
    elif [ -f "$BRIEF_DIR/$_name" ]; then
        printf "%s\n" "$BRIEF_DIR/$_name"
    fi
}

case "${1:-}" in
--status)
    for d in "$ROOT"/*/; do
        [ -d "$d" ] || continue
        name=$(basename "$d")
        pid=$(cat "$d/session.pid" 2>/dev/null || echo "-")
        alive=no
        [ "$pid" != "-" ] && kill -0 "$pid" 2>/dev/null && alive=yes
        report=no
        [ -s "$d/report.md" ] && report=yes
        printf "%-3s pid=%-8s alive=%-4s report=%-4s log=%sB\n" \
            "$name" "$pid" "$alive" "$report" \
            "$(wc -c < "$d/session.log" 2>/dev/null || echo 0)"
    done
    exit 0
    ;;
--resolve-brief)
    # Print the brief each task would use, and nothing else.
    #
    # This answers "which file does the dispatch actually read?" without
    # cloning the repository or starting a session, so it is safe to run
    # anywhere and it is how the lookup is tested. It asks the same
    # function the dispatch asks, so the two cannot disagree.
    shift
    for task in ${*:-I J}; do
        _path=$(brief_path_for_task "$task")
        if [ -n "$_path" ]; then
            printf "%s %s\n" "$task" "$_path"
        else
            printf "%s MISSING %s %s\n" "$task" \
                "$REPO_SRC/docs/$(brief_for_task "$task")" \
                "$BRIEF_DIR/$(brief_for_task "$task")"
        fi
    done
    exit 0
    ;;
esac

TASKS="${*:-I J}"

if ! curl -s -o /dev/null --max-time 8 "$GATEWAY/v1/models"; then
    echo "gateway $GATEWAY unreachable; aborting" >&2
    exit 1
fi

for task in $TASKS; do
    work="$ROOT/$task"
    # Refuse to start a second session for a card that is already running.
    #
    # Two sessions on one working copy do not queue: they edit the same
    # files at the same time, and whichever finishes second overwrites
    # the other's work. The client line has a guard for this (the card
    # guard refuses a task whose state is busy); the kernel line had
    # none, so a re-dispatch was one command away.
    if [ -s "$work/session.pid" ]; then
        _pid=$(cat "$work/session.pid" 2>/dev/null || echo "-")
        if [ "$_pid" != "-" ] && kill -0 "$_pid" 2>/dev/null; then
            echo "$task is already running (pid=$_pid); not dispatching again" >&2
            echo "(stop it first, or use a different task letter)" >&2
            continue
        fi
    fi
    mkdir -p "$work"
    # Pick the brief for this task unless one was forced from the outside.
    task_brief="$BRIEF"
    if [ -z "$task_brief" ]; then
        # Repository copy first, then the out-of-repository directory.
        # See brief_path_for_task and BRIEF_DIR above for why the second
        # one exists.
        task_brief=$(brief_path_for_task "$task")
    fi
    if [ -z "$task_brief" ] || [ ! -f "$task_brief" ]; then
        echo "brief not found for $task" >&2
        echo "(looked in the repository's docs/ and in $BRIEF_DIR)" >&2
        exit 1
    fi
    # The brief has to have a section for this task.
    #
    # The mapping says which file a task reads; the file says what the
    # task is. A brief whose section is missing or renamed sends the
    # card off to read another round's instructions, and the card does
    # not notice -- it reads a plausible document and implements the
    # wrong thing. Checking here turns that into a refusal to dispatch.
    if ! grep -qE "^## 任务 $task[：: ]" "$task_brief"; then
        echo "brief $task_brief has no section for task $task; not dispatching" >&2
        exit 1
    fi
    if [ ! -d "$work/repo/.git" ]; then
        git clone -q "$REPO_SRC" "$work/repo" || {
            echo "clone failed for $task" >&2
            continue
        }
    else
        # Re-sync the working copy to the latest main.
        #
        # The resync used to be inline here, and it destroyed the work it
        # was supposed to preserve: it committed the dirty state and then
        # ran `reset --hard origin/main`, which discarded that commit and
        # deleted untracked files outright. Task N's working copy holds
        # its three envelope source files as untracked files on purpose
        # (they are the card's input, not its output), so the resync would
        # have deleted them and the card would have had nothing to merge.
        #
        # The resync now lives in its own script so it can be tested
        # directly; see tools/rebuild/resync_card_repo.sh.
        if ! bash "$REPO_SRC/tools/rebuild/resync_card_repo.sh" \
            "$work/repo" main; then
            echo "resync failed for $task" >&2
            continue
        fi
    fi

    # Short prompt: point at the brief, name the task, state the hard rules.
    # Keeping this under ~400 chars is the whole point.
    printf '读 %s 的任务 %s，在 %s 里实现。只改该任务列出的文件。写 gtest，用 -Wall -Wextra -Werror -pedantic 编译零告警并跑通。语义冲突写 report.md 不要自己选。' \
        "$task_brief" "$task" "$work/repo" > "$work/prompt.txt"

    cat > "$work/run.sh" <<RUNNER
#!/bin/bash
echo \$\$ > "$work/session.pid"
exec env ANTHROPIC_BASE_URL="$GATEWAY" ANTHROPIC_MODEL="$MODEL" \\
    claude --permission-mode acceptEdits -p "\$(cat "$work/prompt.txt")"
RUNNER
    chmod +x "$work/run.sh"

    : > "$work/session.pid"
    setsid -f bash "$work/run.sh" > "$work/session.log" 2>&1 < /dev/null
    for _ in 1 2 3 4 5 6 7 8 9 10; do
        [ -s "$work/session.pid" ] && break
        sleep 0.3
    done
    echo "started $task pid=$(cat "$work/session.pid" 2>/dev/null || echo '?') prompt=$(wc -c < "$work/prompt.txt")B"
    sleep 1
done
