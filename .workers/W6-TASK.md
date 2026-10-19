# W6 任务：timezone 注入实现（0008，测试先行）

你是 lengsuo-browser 内核仓的开发工人。你的分支：`worker/w6-timezone-impl`
（worktree `w6-timezone-impl`）。只在本分支提交，不 push，不 merge main。

## 阶段 0：设计定稿（不改集成代码）

`docs/batch4-language-timezone-design.md` 的 §2.2/§3.2 是「待填充」，
§3.1 已核实（blink TimeZoneController 收口于进程内 ICU default，
`icu::TimeZone::adoptDefault`，timezone_controller.cc:149-175/:226-249）。
你在 155 固定树（**只读**）完成核实并定稿推荐覆盖点：

- 待核实：browser 进程谁是 `device::mojom::TimeZoneMonitor` 服务方
  （`services/device/time_zone_monitor/`）；renderer 的
  TimeZoneController::Init 只有 devtools/测试两个既有触发方——
  画像通道必须有 browser→renderer 的启动期触发点。候选：
  a) `RendererPreferences` 加字段（mojo 演进，channel 关联）；
  b) renderer 命令行 switch（`blink::switches`，进程级一次）；
  c) content 层在 renderer 启动后调 blink override 的桥。
  论证选一个唯一推荐，写回设计文档并提交（小步：设计一节一提交）。
- 非伪造：无效时区 ID 必须拒绝启动（对齐 language 批次 startup-reject
  契约），不允许回退宿主时区继续跑。wire `timezone` 字段（≤64 可打印）
  即画像值，无需 catalog。

## 阶段 1：仓内红→绿（不动 Chromium 树）

- `integration/` 增加 timezone 状态与访问器（对齐 profile/catalog 模式：
  冻结一次、失败不可替换、const 暴露）；`integration/*_unittest.cc`
  先加失败用例（golden wire 的 `UTC` 解出、非法 ID 拒绝）。
- **禁止触碰** `/home/ubuntu/lengsuo-browser-dev/src`（那是 W1 工人的
  活动区）；你的单测编译验证等阶段 2。

## 阶段 2：树上闭环（门禁）

仅当 `/home/ubuntu/lengsuo-browser-dev/reviews/batch34-linux-reapply2.log`
存在且含 `"status": "applied"`（W1 完成树恢复的标记）才继续；否则把
进度提交到分支后正常退出，注明等待门禁。

- 固定树上：重放 overlay+patches（含你的 0008）、
  `autoninja bootstrap_unittests`（先红后绿）、重建 chrome、跑
  `python3 tests/browser_tz_smoke.py --chrome <src>/out/lengsuo/chrome
  --repo <kernel-repo> --work /home/ubuntu/lengsuo-browser-dev/smoke-tz-linux-1`，
  预期 9/9（红相证据在 Windows 侧：`D:\lengsuo-build\logs\win-tz-smoke-red.log`，
  managed 案实测 Asia/Shanghai/-480）。
- 生成 `patches/0008-*.patch`、重锁 baseline、更新设计文档状态行与
  browser-bootstrap/README 状态，全部提交到本分支。

## 纪律

AGENTS.md（测试先失败再实现/文档随代码/700 行上限）；每小节一提交；
不动 Windows；不与 W1/W3/W5 的 worktree 互相触碰；卡住就写 NOTES.md
提交后退出。
