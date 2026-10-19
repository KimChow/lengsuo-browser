#!/bin/bash
# Launch one headless claude-code session per C-class task.
#
# Each task gets its own working directory seeded from the repository so four
# agents never edit the same file: tasks A/B/C touch different subsystems, and
# D is evidence-only. The shared Chromium tree is read-only for them - they
# stage changes as patches, which is also what the review flow expects.
#
# Usage:
#   bash launch_cc_tasks.sh                 # start all four
#   bash launch_cc_tasks.sh A B             # start selected tasks
#   bash launch_cc_tasks.sh --status        # show running sessions
set -u

GATEWAY="${LENGSUO_GATEWAY:-http://192.168.1.151:18080}"
MODEL="${LENGSUO_CC_MODEL:-auto}"
ROOT=/home/ubuntu/cc-tasks
REPO_SRC=/home/ubuntu/lengsuo-browser-dev/recipe
BRIEF="$REPO_SRC/docs/cc-task-brief.md"
CHROMIUM=/home/ubuntu/lengsuo-browser-dev/src

if [ "${1:-}" = "--status" ]; then
    for d in "$ROOT"/*/; do
        [ -d "$d" ] || continue
        name=$(basename "$d")
        pid=$(cat "$d/session.pid" 2>/dev/null || echo "-")
        alive=no
        [ "$pid" != "-" ] && kill -0 "$pid" 2>/dev/null && alive=yes
        printf "%-6s pid=%-8s alive=%-4s log=%s\n" "$name" "$pid" "$alive" \
            "$(wc -c < "$d/session.log" 2>/dev/null || echo 0)B"
    done
    exit 0
fi

TASKS="${*:-A B C D}"

# Gateway reachability is checked once: every session depends on it, and a
# silent failure here would look like an agent doing nothing.
if ! curl -s -o /dev/null --max-time 8 "$GATEWAY/v1/models"; then
    echo "gateway $GATEWAY unreachable; aborting" >&2
    exit 1
fi

if [ ! -f "$BRIEF" ]; then
    echo "task brief not found at $BRIEF" >&2
    exit 1
fi

for task in $TASKS; do
    work="$ROOT/$task"
    mkdir -p "$work"
    if [ ! -d "$work/repo/.git" ]; then
        git clone -q "$REPO_SRC" "$work/repo" || {
            echo "clone failed for $task" >&2
            continue
        }
    else
        git -C "$work/repo" fetch -q origin main && \
            git -C "$work/repo" reset -q --hard origin/main
    fi

    # The brief and the shared tree location are passed as context; the agent
    # reads the brief itself so the prompt stays short and reviewable.
    prompt="阅读 $BRIEF，只做其中的任务 $task。
工作目录是 $work/repo（这是一个独立 clone，可以自由改）。
Chromium 构建树在 $CHROMIUM（只读参考，不要在里面改我们自己的源码）。
产出：patches/ 下的补丁 + 对应测试 + 简短报告，写进 $work/report.md。
遇到语义冲突就停下来写进报告，不要自行选一条路。"

    ANTHROPIC_BASE_URL="$GATEWAY" \
    ANTHROPIC_MODEL="$MODEL" \
    setsid nohup claude --model "$MODEL" --permission-mode acceptEdits \
        -p "$prompt" > "$work/session.log" 2>&1 < /dev/null &
    echo $! > "$work/session.pid"
    echo "started $task pid=$(cat "$work/session.pid")"
    sleep 1
done

echo "logs under $ROOT/<task>/session.log ; check with --status"
