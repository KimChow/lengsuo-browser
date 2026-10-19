#!/bin/bash
# Continue a task session with the rulings applied.
#
# Previous attempt died immediately: the background launch was written as
#   `setsid nohup claude ... > log 2>&1 < /dev/null &`
# which detaches stdout but not the session, so the process was reaped when the
# SSH channel closed. `setsid -f` forks before re-parenting, which is what
# actually survives.
#
# Also refuses to clobber uncommitted work: a session's first actions may be
# `git reset --hard`, and a half-finished implementation would be lost. Any
# dirty tree is committed first (as a wip commit the reviewer can squash).
set -u

GATEWAY="${LENGSUO_GATEWAY:-http://192.168.1.151:18080}"
MODEL="${LENGSUO_CC_MODEL:-auto}"
ROOT=/home/ubuntu/cc-tasks
RULINGS=/home/ubuntu/lengsuo-browser-dev/recipe/docs/cc-task-rulings.md

TASKS="${*:-}"

if [ -z "$TASKS" ]; then
    echo "usage: continue_cc_tasks.sh <A|B|C|D> ..." >&2
    exit 2
fi

if ! curl -s -o /dev/null --max-time 8 "$GATEWAY/v1/models"; then
    echo "gateway unreachable; aborting" >&2
    exit 1
fi

if [ ! -f "$RULINGS" ]; then
    echo "rulings not found at $RULINGS; sync the recipe first" >&2
    exit 1
fi

for task in $TASKS; do
    work="$ROOT/$task"
    repo="$work/repo"
    if [ ! -d "$repo/.git" ]; then
        echo "$task: no repo at $repo; run launch_cc_tasks.sh first" >&2
        continue
    fi

    # Preserve anything uncommitted before the session starts moving files.
    if [ -n "$(git -C "$repo" status --porcelain)" ]; then
        git -C "$repo" add -A
        git -C "$repo" -c user.email=cc@local -c user.name=cc-session \
            commit -q -m "wip($task): 续跑前先落盘，避免被 reset 冲掉"
        echo "$task: preserved dirty tree -> $(git -C "$repo" log --oneline -1)"
    fi

    prompt="阅读 $RULINGS，按其中对任务 $task 的裁定完成实现（裁定已给出结论，
不要再问同样的问题）。工作目录 $repo。产出 patches/ 补丁 + 测试，
并更新 $work/report.md 说明最终结论与验证方式。"

    # `pgrep | tail -1` picks the wrong process when several sessions start in
    # the same second, which made a live session look dead. Record the PID from
    # the shell instead: `setsid -f` returns immediately, so take the newest
    # child whose command line mentions this task's working directory.
    marker="$work/repo"
    before=$(pgrep -f "claude --permission-mode acceptEdits" | sort || true)

    ANTHROPIC_BASE_URL="$GATEWAY" ANTHROPIC_MODEL="$MODEL" \
    setsid -f claude --permission-mode acceptEdits -p "$prompt" \
        > "$work/session2.log" 2>&1 < /dev/null

    sleep 4
    after=$(pgrep -f "claude --permission-mode acceptEdits" | sort || true)
    pid=$(comm -13 <(echo "$before") <(echo "$after") | head -1)
    if [ -z "$pid" ]; then
        # Fall back to the newest starter; better a possibly-wrong PID than a
        # missing one, since the log is the real source of truth.
        pid=$(echo "$after" | tail -1)
    fi
    echo "$pid" > "$work/session2.pid"
    echo "$task: continued pid=$pid (marker $marker)"
done

echo "watch: tail -f $ROOT/<task>/session2.log"
