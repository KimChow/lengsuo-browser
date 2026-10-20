#!/bin/bash
# Launch one headless claude-code session per L3 storage-layer task (E/F/G/H).
#
# The four tasks touch disjoint new files, so a shared read-only view of the
# repository is enough: each agent writes only its own native/*_test.cc plus
# implementation, and stages the result as a patch. That keeps review simple
# and avoids merge conflicts between agents.
#
# Usage:
#   bash launch_l3_tasks.sh              # start all four
#   bash launch_l3_tasks.sh E F          # start selected tasks
#   bash launch_l3_tasks.sh --status     # show running sessions
#   bash launch_l3_tasks.sh --stop       # stop all running sessions
set -u

GATEWAY="${LENGSUO_GATEWAY:-http://192.168.1.151:18080}"
MODEL="${LENGSUO_CC_MODEL:-auto}"
ROOT=${LENGSUO_L3_ROOT:-/home/ubuntu/l3-tasks}
REPO_SRC=${LENGSUO_REPO_SRC:-/home/ubuntu/lengsuo-browser-dev/recipe}
BRIEF="$REPO_SRC/docs/cc-task-brief-l3.md"

case "${1:-}" in
--status)
    for d in "$ROOT"/*/; do
        [ -d "$d" ] || continue
        name=$(basename "$d")
        pid=$(cat "$d/session.pid" 2>/dev/null || echo "-")
        alive=no
        [ "$pid" != "-" ] && kill -0 "$pid" 2>/dev/null && alive=yes
        printf "%-4s pid=%-8s alive=%-4s log=%s\n" "$name" "$pid" "$alive" \
            "$(wc -c < "$d/session.log" 2>/dev/null || echo 0)B"
    done
    exit 0
    ;;
--stop)
    for d in "$ROOT"/*/; do
        [ -d "$d" ] || continue
        pid=$(cat "$d/session.pid" 2>/dev/null || echo "")
        [ -n "$pid" ] && kill -0 "$pid" 2>/dev/null && kill "$pid" && echo "stopped $pid"
    done
    exit 0
    ;;
esac

TASKS="${*:-E F G H}"

# Every session depends on the gateway; a silent failure here would look like
# an agent doing nothing at all.
if ! curl -s -o /dev/null --max-time 8 "$GATEWAY/v1/models"; then
    echo "gateway $GATEWAY unreachable; aborting" >&2
    exit 1
fi

if [ ! -f "$BRIEF" ]; then
    echo "task brief not found at $BRIEF" >&2
    exit 1
fi

# Model routing: ANTHROPIC_MODEL is what the gateway reads. Passing --model
# instead makes Claude Code warn about an unknown alias, which buries real
# warnings without changing behaviour.
for task in $TASKS; do
    work="$ROOT/$task"
    mkdir -p "$work"
    if [ ! -d "$work/repo/.git" ]; then
        git clone -q "$REPO_SRC" "$work/repo" || {
            echo "clone failed for $task" >&2
            continue
        }
    else
        # Preserve any uncommitted work before syncing: a previous run may have
        # produced implementation that was never committed.
        if [ -n "$(git -C "$work/repo" status --porcelain 2>/dev/null)" ]; then
            git -C "$work/repo" add -A >/dev/null 2>&1
            git -C "$work/repo" -c user.email=wip@local -c user.name=wip \
                commit -q -m "wip: auto-stash before resync" >/dev/null 2>&1
        fi
        git -C "$work/repo" fetch -q origin main && \
            git -C "$work/repo" reset -q --hard origin/main
    fi

    prompt="阅读 $BRIEF，只做其中的任务 $task。
工作目录是 $work/repo（独立 clone，可自由改）。
只新建/修改任务书里「只改这些文件」列出的文件，不要碰其它源码。
验收必须写成 gtest，并实际跑通（给出命令与结果）。
遇到语义冲突就停下来写进 $work/report.md，不要自行选一条路。"

    # setsid -f forks and returns immediately without leaving a child PID, so
    # $! is unset. The wrapper records its own PID before exec'ing claude;
    # exec keeps the same PID, so session.pid stays valid for the whole run.
    cat > "$work/run.sh" <<RUNNER
#!/bin/bash
echo \$\$ > "$work/session.pid"
exec env ANTHROPIC_BASE_URL="$GATEWAY" ANTHROPIC_MODEL="$MODEL" \\
    claude --permission-mode acceptEdits -p "\$(cat "$work/prompt.txt")"
RUNNER
    printf '%s' "$prompt" > "$work/prompt.txt"
    chmod +x "$work/run.sh"

    : > "$work/session.pid"
    setsid -f bash "$work/run.sh" > "$work/session.log" 2>&1 < /dev/null
    # wait briefly for the wrapper to publish its pid
    for _ in 1 2 3 4 5 6 7 8 9 10; do
        [ -s "$work/session.pid" ] && break
        sleep 0.3
    done
    echo "started $task pid=$(cat "$work/session.pid" 2>/dev/null || echo '?')"
    sleep 1
done

echo "logs under $ROOT/<task>/session.log ; check with --status"
