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
esac

TASKS="${*:-I J}"

if ! curl -s -o /dev/null --max-time 8 "$GATEWAY/v1/models"; then
    echo "gateway $GATEWAY unreachable; aborting" >&2
    exit 1
fi

for task in $TASKS; do
    work="$ROOT/$task"
    mkdir -p "$work"
    # Pick the brief for this task unless one was forced from the outside.
    task_brief="$BRIEF"
    if [ -z "$task_brief" ]; then
        case "$task" in
            O)   task_brief="$REPO_SRC/docs/cc-task-brief-l3e.md" ;;
            N|Q|R) task_brief="$REPO_SRC/docs/cc-task-brief-l3f.md" ;;
            M)   task_brief="$REPO_SRC/docs/cc-task-brief-l3d.md" ;;
            *)   task_brief="$REPO_SRC/docs/cc-task-brief-l3b.md" ;;
        esac
    fi
    if [ ! -f "$task_brief" ]; then
        echo "brief not found: $task_brief" >&2
        exit 1
    fi
    if [ ! -d "$work/repo/.git" ]; then
        git clone -q "$REPO_SRC" "$work/repo" || {
            echo "clone failed for $task" >&2
            continue
        }
    else
        if [ -n "$(git -C "$work/repo" status --porcelain 2>/dev/null)" ]; then
            git -C "$work/repo" add -A >/dev/null 2>&1
            git -C "$work/repo" -c user.email=wip@local -c user.name=wip \
                commit -q -m "wip: auto-stash before resync" >/dev/null 2>&1
        fi
        git -C "$work/repo" fetch -q origin main && \
            git -C "$work/repo" reset -q --hard origin/main
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
