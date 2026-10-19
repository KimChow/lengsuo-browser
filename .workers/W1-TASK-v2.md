# W1 任务（v2）：Linux 侧 0006/0007 实证闭环

你是 lengsuo-browser 内核仓的开发工人。你的分支：`worker/w1-ua-uach`（已含
0006 红相 WIP 提交 3ac890f，该 WIP 已被 main 上的正式实现取代——
先 `git checkout main -- integration native patches 2>/dev/null; git reset --hard origin/main` 对齐后开工，
或直接 `git merge origin/main`，以 main 为准）。

## 背景（必读）

main（origin/main，≥c085c18）已包含：
- `patches/0006-browser-ua-catalog.patch`（UA/UA-CH 双出口 + catalog 加载）
- `patches/0007-profile-language.patch`（accept_language RendererPreferences 链）
- `tests/browser_ua_smoke.py`（9 路径浏览器冒烟：3 正 6 负）
- `docs/batch3-ua-uach-design.md` 声称「两平台 8/8 单测 + 两平台真浏览器验证」。

**问题**：Windows 侧证据完整（D:\lengsuo-build 树 + chrome.exe 2026-10-17 09:28
+ 主会话独立复跑 9/9 通过），但 **Linux 侧（本机 1.47）的固定树还没有 0006/0007**——
声明中的 Linux 证据缺失，必须由你闭环。

## 环境

- Chromium 固定树（只有你允许改动）：`/home/ubuntu/lengsuo-browser-dev/src`
  （当前：0001–0005 已应用 + 旧 overlay；out/lengsuo 有增量缓存）
- depot_tools：`export PATH=/home/ubuntu/lengsuo-browser-dev/depot_tools:$PATH`
- 仓库 main：`/home/ubuntu/lengsuo-browser-dev/kernel-repo`（c085c18）
- 证据日志：`/home/ubuntu/lengsuo-browser-dev/reviews/`（命名 `batch34-linux-*.log`）

## 步骤

1. 固定树先还原干净（`git -C <src> checkout HEAD -- chrome/ content/` 等，
   保留 lengsuo/ overlay 目录由 overlay 工具重放），然后按 main 的
   baseline.json（含 0006/0007）：
   `python3 -m tools.overlay --source <src> --repository <kernel-repo> --lock <kernel-repo>/baseline.json --apply --allow-patched-baseline`
   `python3 -m tools.patch_apply --source <src> --repository <kernel-repo> --lock <kernel-repo>/baseline.json --apply`
2. `autoninja -C <src>/out/lengsuo bootstrap_unittests` → 运行，
   预期 **8/8**（5 原有 + catalog/UA 新增 3；以实际用例数为准，记录日志）。
3. `autoninja -C <src>/out/lengsuo chrome`（增量，先 bootstrap 后 chrome）。
4. 运行 `python3 tests/browser_ua_smoke.py --chrome <src>/out/lengsuo/chrome
   --repo <kernel-repo> --work /home/ubuntu/lengsuo-browser-dev/smoke-ua-linux-1`
   （work 目录必须新建）。预期 managed/upstream/profile-only rc=0 且
   字段与 Windows 侧一致，6 条负路径 rc=5。
5. 全部证据写 `reviews/`；在**你的分支**更新
   `docs/batch3-ua-uach-design.md` 的复现与证据节（补 Linux 侧命令与日志路径）
   并提交。不要 push，不要直接改 main。
6. 若任何一步失败：记录日志与诊断到 NOTES.md 提交，不要为了绿而放宽断言。

## 禁止

不动 Windows；不改 tests/browser_ua_smoke.py 的断言语义；
不伪造任何值（合成 Synth/1.0 体系不变）。
