#!/bin/sh
# 在 47 上**分离式**派一张客户端线的卡。
#
# ⭐⭐ 为什么必须有这个脚本（2026-10-22 18:4x 实测踩到）：
#
#   直接 `ssh 47 'python3 bounded_worker_runner.py <卡名>'` 会**当场把卡杀掉**。
#
#   实测：2026-10-22 18:18 派的 T4 卡，18:21 就死了，**工作树零产出**（25 分钟
#   全花在调研「docs/ 能不能提交」上）。排查结论：
#
#   - 内核线用 `setsid -f bash run.sh`（launch_l3b_short.sh 第 230 行）⇒ 会话与
#     发起方解绑 ⇒ SSH 断开不影响卡。
#   - 编排器用 `subprocess.Popen(..., start_new_session=True)`（orchestrator.py
#     第 382 行）⇒ 同样解绑。
#   - ⛔ **客户端线的手工派卡路径两者都没有** ⇒ runner 留在发起方 SSH 会话的
#     进程组里 ⇒ 会话一结束，整组进程被 SIGTERM ⇒ 卡连同 claude 子进程一起死。
#
#   ⇒ 本脚本用 `setsid` 把 runner 放进**新会话**，与发起方彻底解绑。
#   派完可以立刻断开 SSH，卡继续跑。
#
# 用法：
#   ssh 47 'bash /home/ubuntu/lengsuo-browser-dev/dispatch_desktop_card.sh <卡名>'
#
# 前置（派卡方要自己保证，脚本只管起会话）：
#   ① `tasks/<卡名>.txt` 与 `tasks/<卡名>.json` 都在（文件名必须与卡名逐字一致）；
#   ② worktree 已手工建好（派卡器**不会**建）：
#      git -C repo worktree add -b card/<卡名> \
#          /home/ubuntu/lengsuo-desktop-dev/worktrees/<卡名> origin/main
#   ③ `state/<卡名>.json` 不是 running（否则 card_guard 会拒派）。
#
# 退出码：0 = 已派出；非 0 = 派出失败（看 stderr）。

set -e

TASK="${1:-}"
if [ -z "$TASK" ]; then
    echo "用法: dispatch_desktop_card.sh <卡名>" >&2
    exit 2
fi

ROOT=/home/ubuntu/lengsuo-desktop-dev
cd "$ROOT"

if [ ! -f "tasks/$TASK.txt" ]; then
    echo "FATAL: tasks/$TASK.txt 不存在（卡名与任务书文件名必须逐字一致）" >&2
    exit 2
fi
if [ ! -f "tasks/$TASK.json" ]; then
    echo "FATAL: tasks/$TASK.json 不存在" >&2
    exit 2
fi
if [ ! -d "worktrees/$TASK" ]; then
    echo "FATAL: worktrees/$TASK 不存在（派卡器不会建 worktree，必须手工建）" >&2
    exit 2
fi

mkdir -p logs

# ⭐ 解绑的关键：setsid 让 runner 进入新会话（setsid 在 util-linux 里，
# 若已是组长会先 fork；`|| true` 只为容忍「已经是组长」的情形）。
setsid python3 bounded_worker_runner.py "$TASK" \
    >> "logs/$TASK.out" 2>&1 < /dev/null &
RUNNER_PID=$!

sleep 2

# 记下 pid 便于事后核对；state/<卡名>.json 由 runner 自己写（claim 阶段）。
echo "$RUNNER_PID" > "logs/$TASK.runner.pid"

# 核验：runner 还在不在 + state 是不是 running
if kill -0 "$RUNNER_PID" 2>/dev/null; then
    echo "OK: $TASK 已派出（runner pid=$RUNNER_PID，新会话，可断开 SSH）"
    cat "state/$TASK.json" 2>/dev/null || true
    exit 0
fi

echo "FATAL: runner pid=$RUNNER_PID 已经退出 ⇒ 派卡失败" >&2
tail -20 "logs/$TASK.out" 2>/dev/null >&2 || true
exit 1
