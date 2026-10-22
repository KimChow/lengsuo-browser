# 环境通道任务报告索引 —— 语义冲突与待裁定项

> **每张卡只往 `docs/l3-reports/task-<卡名>-conflicts.md` 写**，本文件只做索引与
> 当前未决项汇总，不再堆正文。
>
> ⛔ 遇到语义冲突**停下来写进对应卡的那份报告**，不要自己选一条路。
> ⛔ 根 `report.md` 已于 2026-10-22 拆分为索引：它曾涨到 760 行、超过 700 行硬上限
> （`tools/check_line_limit.py` 实测 `FAIL report.md: 760 > 700`），
> 拆分本身即那条硬上限的修复。

## 报告在哪

| 卡 | 冲突与待裁定项 | 完整报告（做了什么 / 设计取舍 / 验收） |
|---|---|---|
| **O** | `docs/l3-reports/task-O-conflicts.md` | `docs/l3-reports/task-O.md` |
| **Q** | `docs/l3-reports/task-Q-conflicts.md` | `docs/l3-reports/task-Q.md` |
| **N** | `docs/l3-reports/task-N-conflicts.md` | `docs/l3-reports/task-N.md` |
| **R** | `docs/l3-reports/task-R-conflicts.md` | （本卡无卡侧报告，见 R-C） |

> `docs/` 只留本地、不入库 ⇒ 这些报告不随仓库分发，也不在 git 历史里。

## 当前未决项（需要主理人裁定）

### ⛔ 阻断性

| 来源 | 项 | 一句话 |
|---|---|---|
| N-A | 磁盘文档用哪把钥匙 | 会话密钥只活一个会话，磁盘文档跨会话 ⇒ 落盘信封的钥匙来源未定 |
| N-B | 两层信封要不要合并 | 落盘 L3E1 与管道 §3.1 的信封字节序/KDF/AAD/布局全不同 |
| R-C | 客户端侧 R 卡未交卷 | 内核侧已合入，客户端侧当时仍未交卷（后续已补，见 R-E） |
| R-D | 内核侧 `profile_bundle` 无调用者 | 该 `source_set` 是叶子节点 ⇒ 有 19 条 gtest 但**不进 chrome 二进制** |

### 🟡 待复核

| 来源 | 项 |
|---|---|
| O-A / O-B | 任务书给的 renderer 拉取位置 / binder 挂载点与 155 树的实际结构不符（卡已按树里实际写法落地） |
| O-D | 超出允许清单的我方文件（已改，需追认） |
| O-E | 顺手修掉的静默失效测试（请复核这个判断） |
| O-F | `tools/rebuild/apply_l3_wiring.py` 重写（请复核） |
| O-G | 任务书建议与实际不符之处（只报告，未自行扩张） |
| O-H | 验收覆盖分层（任务书第 6 条要求写清） |
| Q-A | 任务书说「不许写任何加密实现」，但任务 N 的信封不是管道那个信封 |
| Q-D | 会话密钥的生成方式（随机 vs HKDF）—— 两侧已按「可注入」落地，拍板后只改调用方一行 |
| Q-E | 越出允许清单的改动（已改，需追认） |
| Q-G | 上行事件的采集点不在任务 Q 授权范围内 |
| Q-H | `docs/env-channel-design.md` 与线契约不一致（契约赢） |
| Q-I | 「失败只计数」的计数落在哪里（请复核） |
| Q-J | 管道按依赖拆成五层（已实现） |
| Q-K | `integration/kernel_pipe_client.cc` 453 行，超 400 行软目标（请复核） |
| Q-L | Chromium 155 的 API 与任务书预期不符（三处，均按树里实际 API 写） |
| N-C | 会话密钥过期后，磁盘信封怎么办 |
| N-D | 迭代次数 100000 实测 8 ms/次，与注释里写的「~20 ms」有出入 |
| N-E | `l3_envelope.h` 里有一句关于 `--lengsuo-managed-l3` 的过期注释 |
| N-F | Chromium 工具链下两个 `.cc` 有 24 条 `-Wunsafe-buffer-usage` 告警 |
| N-G | 两处「不在允许清单里 ⇒ 本卡做不了」的缺口 |
| N-H | `tests/test_patch_replay.py` 的 required 集合没加新路径 |
| N-I | 补丁纪律踩坑：relock 必须在 commit **之后** |
| R-B | 内核侧**不产出**契约 §4.5 那个 JSON 清单 —— 这是有意的，不是缺口 |

### ✅ 已裁定（留档，不必再议）

| 来源 | 项 | 裁定 |
|---|---|---|
| Q-B | 管道名 → OS 端点的映射 | 2026-10-21 13:49，主线提交 `b30922f` |
| Q-C | 契约内部的历史矛盾 | 已在主线修好，Q 按修好的版本实现 |
| Q-F | 契约的两处补充 | 2026-10-21 13:49，主线提交 `b30922f` |
| R-A | 内核侧已合入，闸门自复跑全绿 | 见 `docs/l3-reports/task-R-conflicts.md` R-A |
| R-E | 真编译 + accept 全绿，客户端侧也已合入 | 同上 R-E |
| R-F | R 的跨仓向量已成常驻守卫 | 同上 R-F |

## 已并入主线的裁定（不在本文件）

拍板单与逐条状态在 `docs/env-channel-tasklist.md` 的「待主理人拍板」表
（含 10 条）与「🔴 拍板单」段。
