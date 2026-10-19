#!/bin/bash
# Launch one claude code session per task, each in its own worktree.
#
# All sessions use the local gateway's `auto` model. Worktrees are separate so
# four agents cannot edit the same file concurrently - concurrent writes to one
# working tree is what corrupts this kind of run, not the model choice.
#
# Usage: ./run_cc_tasks.sh <task>       # task = a|b|c|d
set -u

GATEWAY="http://192.168.1.151:18080"
REPO="/home/ubuntu/lengsuo-browser-dev/recipe"
BASE="/home/ubuntu/cc-work"
BRANCH_BASE="${LENGSUO_BASE_BRANCH:-main}"
CHROME_TREE="/home/ubuntu/lengsuo-browser-dev/src"

task="${1:-}"
case "$task" in
  a) name="gpu-switcher" ;;
  b) name="catalog-meta" ;;
  c) name="fonts" ;;
  d) name="webgpu" ;;
  *) echo "usage: $0 <a|b|c|d>"; exit 2 ;;
esac

WT="$BASE/wt-$task"
LOG="$BASE/log-$task.txt"

# The checkout is a shallow clone, so worktree add would fail on a missing base
# branch. Fall back to a plain clone of the local repo, which is self-contained.
if [ ! -d "$WT/.git" ] && [ ! -f "$WT/.git" ]; then
    if ! git -C "$REPO" worktree add -B "cc-$name" "$WT" "$BRANCH_BASE" \
            >"$BASE/wt-$task.setup.log" 2>&1; then
        git clone -q -b "$BRANCH_BASE" "$REPO" "$WT" \
            >>"$BASE/wt-$task.setup.log" 2>&1 || {
                echo "worktree setup failed; see $BASE/wt-$task.setup.log"
                exit 1; }
        git -C "$WT" checkout -q -B "cc-$name" "$BRANCH_BASE"
    fi
fi

PROMPT="你在 $WT 这个 git 工作副本里工作（Chromium 内核仓 lengsuo-browser 的一个分支）。

完整任务书在 $BASE/TASK_BRIEF.md，先通读它，然后只做「任务 ${task^^}」那一节。

必须遵守的硬约束（TASK_BRIEF.md「通用约束」里也有，这里再说一遍）：
1. 先写失败测试再实现；没有测试的代码视为未完成。
2. 补丁要能从干净基线重放，只动必要的最少代码，不改上游语义。
3. 单文件 400 行软目标、700 行硬上限；超了拆文件。
4. 换行符一律 LF（write_bytes，不要 write_text）。
5. 不提交凭据、密钥、真实数据、构建产物。
6. 完成后跑：python3 -m unittest discover -s tests（要全绿）、
   python3 tools/check_line_limit.py（要 0 告警）。
7. 遇到语义冲突就停下来写进报告，不要自行选一条路闷头做。

Chromium 构建树在 $CHROME_TREE（注意不是 chromium-src），构建方式见
tools/rebuild/README.md。需要编译验证时把源码复制到那个树再编，
但注意：四个任务可能同时编译，**编译前先确认没有别的 ninja 在跑**
（pgrep -c ninja），有就等一会儿。

产出：patches/ 下的补丁 + 测试 + 在 $WT/CC-REPORT.md 写一份简短报告
（做了什么、证据是什么、哪些没做、为什么）。"

setsid nohup env \
    ANTHROPIC_BASE_URL="$GATEWAY" \
    ANTHROPIC_AUTH_TOKEN="local" \
    ANTHROPIC_MODEL="auto" \
    ANTHROPIC_DEFAULT_OPUS_MODEL="auto" \
    ANTHROPIC_DEFAULT_SONNET_MODEL="auto" \
    ANTHROPIC_DEFAULT_HAIKU_MODEL="auto" \
    CLAUDE_CODE_DISABLE_NONESSENTIAL_TRAFFIC="1" \
    claude --model auto --permission-mode acceptEdits -p "$PROMPT" \
    > "$LOG" 2>&1 < /dev/null &

echo "started task=$task log=$LOG worktree=$WT pid=$!"

