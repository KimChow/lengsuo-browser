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
| **M0** | `docs/l3-reports/task-M0-conflicts.md` | `docs/l3-reports/task-M0.md` |
| **凭据线 W** | `docs/credential-dispatch-plan.md` §8.3（W6/扩展 ID）/ §8.5（W5 派卡实录） | （同左；本轮为派卡方取证，非卡侧报告） |

> `docs/` 只留本地、不入库 ⇒ 这些报告不随仓库分发，也不在 git 历史里。

## 当前未决项（需要主理人裁定）

### ⛔ 阻断性

| 来源 | 项 | 一句话 |
|---|---|---|
| N-A | 磁盘文档用哪把钥匙 | 会话密钥只活一个会话，磁盘文档跨会话 ⇒ 落盘信封的钥匙来源未定 |
| N-B | 两层信封要不要合并 | 落盘 L3E1 与管道 §3.1 的信封字节序/KDF/AAD/布局全不同 |
| R-C | 客户端侧 R 卡未交卷 | 内核侧已合入，客户端侧当时仍未交卷（后续已补，见 R-E） |
| R-D | 内核侧 `profile_bundle` 无调用者 | 该 `source_set` 是叶子节点 ⇒ 有 19 条 gtest 但**不进 chrome 二进制** |
| **M-A** | **客户端侧 M0 卡未交卷** | 内核侧已交卷 ⇒ **「跨仓逐字节相同」这一条尚未验证**（互验工具已就位，客户端一发布就是活守卫）|
| **W-EXT-ID** | **凭据线扩展 ID 常量与 Chromium 155 实际算法不符** | 契约 §5.0 写 `SbzJDF-u75AnlyE5`（16 字符 base64url），Chromium 实际必是 **32 字符 `a`–`p`**；且扩展 manifest 缺 `key` 字段 ⇒ unpacked 扩展的 ID 随路径漂移。**W3（Rust host 校验扩展 ID）被卡住** ⇒ 详见 `docs/credential-dispatch-plan.md` §8.3 |
| **W-EXT-DIRNAME** | **凭据线扩展目录名常量与扩展仓实际目录名不一致** | 契约 §5.0 写 `lengsuo-credential`，扩展仓实际目录是 `credential`（构建产物 `dist/credential/`）⇒ **内核包落地 / 安装器接线那一步需要裁定**：内核包里目录叫哪个。**不阻塞 W5**（该卡不硬编码目录名）⇒ 详见 `docs/credential-dispatch-plan.md` §8.5 |
| **W-EXT-SORT** | **扩展仓 JS 的排序口径与契约 §5.5 不一致** | 契约要求 UTF-8 字节序；JS `sort` 用 UTF-16 码元序 ⇒ **BMP 内一致、astral 字符上分叉**（已实测）。当前扩展文件名全 ASCII ⇒ **无实际影响**；将来出现 astral 文件名两侧摘要会不同 ⇒ 详见 `docs/credential-dispatch-plan.md` §8.5 |
| **W-REC-CASE** | **恢复码输入的大小写宽容度** | W2 二层沿用层一 `base32_decode` 口径：**严格大写、不折叠** ⇒ 小写被拒。⚠️ 若要放宽，**客户端侧必须同步改**（`credential-crypto/src/kdf.rs`），否则又是一处跨仓漂移 ⇒ 详见 `docs/credential-dispatch-plan.md` §9.5 #1 |
| **W-REC-RATE** | **恢复码限速参数未定** | 契约与本仓**都没有**任何限速规定（窗口 / 次数 / 锁定时长）⇒ 二层只定义 `RecoveryLimiter` **接口 + 调用方注入**，未内置任何取值 ⇒ 请主理人定实际参数 |
| **W-REC-TBL** | **凭据表新增两个持久化字段** | `PasskeyInfo` 加了 `flags: u16`（**必需** —— 私钥 AAD = `固定头(24) ‖ rp_id`，`flags` 在 `[22..24]` ⇒ 不存它就**重建不出 AAD**、私钥永远解不开）+ `wrapped_dek_recovery: Option<[u8;60]>`（恢复码路径的包裹）⇒ **凭据表结构变了，请复核** |
| **W-REC-CONTRACT** | **契约 §4 缺两条语义** | ① 没写「恢复码解封的是哪一层」（实为**两级解包**：恢复码 → 真 DEK → 私钥明文，**不解环境授权**）；② 没写两个包裹字段的关系（`wrapped_dek_server` = 真 DEK 被**服务端主密钥**包裹；`wrapped_dek_recovery` = **同一个真 DEK** 被**恢复码派生的 DEK** 包裹、AAD 为**空** ⇒ 两条路径拿到的是**同一把 DEK**，不是两把）⇒ 请补进契约（契约是唯一真相） |
| **W-POL-AUDIT-ID** | **`device_id` 原文入审计**（W2 三层卡自报「取记原文」并请复核） | 审计要能追「哪台机器」，但 `device_id` 是设备标识 ⇒ 请复核**审计留存期 / 可见范围**是否与该敏感级别匹配。卡侧报告 §5 已显式声明此选择 |
| **W-POL-ERRCODE** | **`PolicyError::AuditFailed` 新增变体未接错误码** | 变体是 fail-closed 的必要载体（否则「审计写失败」与「准入被拒」无法区分）；⭐ 但**本卡未碰 `src/error.rs`**（在卡边界外）⇒ **HTTP 层暂时无法把它映射成错误码** ⇒ 需后续接线卡处理。⇒ 请确认该变体是否需带载荷（失败原因 / `StoreError`） |
| **W-POL-ERRTYPE** | **`SecretError`（存储层）被塞进 `AdmissionDeny`（准入拒绝原因）** | 类型上「准入被拒」与「存储故障」有点混；当前靠**不同变体 + 不同审计 `outcome` 标签**区分（`store_error` vs 其它）⇒ 若上层要求「拒绝原因统一是业务语义」，需换成独立新类型 |
| **W-POL-POOLVER** | **`pool_version` 语义（版本号相等 ⇒ 同具体值）的证明责任** | 本仓**没有值池表**（契约 `04-fingerprint.md` / `02-environment.md` 状态均为「**提案，待签核**」）⇒ 卡**只做相等比较**，不解释「同版本 ⇒ 同值」⇒ 该前提应由**契约**负责 |
| **W-NM-ENVELOPE** | **Native Messaging 的 JSON 信封 vs 契约 §5.2 的二进制帧** | Chrome 宿主侧只认「**4 字节长度前缀 + JSON**」（`credential-final-plan.md` §3.2 明写），而契约 §5.2 定的是 `[u32 len][u8 type][u8 reserved×3][payload]` ⇒ 二者是**不同层**。⭐ **2026-10-22 19:20 派卡方在 Chromium 155 源码里查清了层次**：「业务消息帧」（扩展⇄宿主）的字节真相是**契约 §5.2**；「管道帧」（宿主进程⇄Chromium）的字节真相是 **Chromium 155 自身实现**（`native_message_process_host.cc`：帧头 **4 字节 u32 本机字节序** + **JSON 载荷**）。⇒ 剩下未裁定的只有「**业务帧 ⇄ JSON 怎么映射**」（裸字节？base64？hex？字段名？）⇒ 阻塞 **W3b-2**（扩展 ID 校验 + host 注册 + 实际 IO）。⭐ **W3b-1（管道传输层）已派且不受此阻塞**（它只管装 JSON 的管道帧，不管 JSON 里装什么）|
| **W-HTTP-DEVID** | **凭据解锁路由的 `device_id` 从哪来？** | 契约 `04-fingerprint.md` §7.5 写「登录态：校验登录态并解析出 `tenant_id` / `user_id` / **`device_id`**」；但 `AuthContext` 只有 `tenant_id`/`user_id`/`permissions`（`auth.rs:62-68`），**没有 `device_id`**；`TokenClaims`（`auth.rs:36-47`）也没有 ⇒ **「从登录态解析」在当前实现下无源可解**。三个候选各带后果：(a) 请求体带 `device_id`（像 `handle_exchange` 那样）⇒ 但那不是「登录态」，与 §7.5 字面不符；(b) 给 `AuthContext` 加字段 ⇒ 动 `auth.rs` + 契约 07 的鉴权边界；(c) 从 `store`（环境）反查 ⇒ **但 `lengsuo-coord` 没有 store 表**（`02-environment.md` 的 `POST /stores` 属另一域）。⇒ **阻塞 W2 的 HTTP 接线卡**（服务端线 `coord` lane 本轮空闲，唯一卡点就是这个 + 下一条）⇒ 详见 `docs/credential-dispatch-plan.md` §10.6 |
| **W-HTTP-PERM** | **凭据解锁路由要不要权限码？用哪个？** | `unlock.rs` 的 `UnlockAuditContext.permission` 文档写：「本模块**不提供默认值、也不新造权限码**……留空表示『本次解锁没有对应的权限码』（默认）；**接线卡知道该填哪个**」；但 `auth.rs` 的 `DEVICE_PERMISSIONS` 只有 4 个（`device:register/heartbeat/read/exchange`）、`scope.rs` 的 3 个是远端作用域 ⇒ **不存在 `credential:unlock` 这个码**。⇒ 接线卡要么**新增**权限码（⇒ 改动封闭集合 + `tests/scope.rs` 钉死的不变量 ⇒ **契约级**动作），要么**留空**（⇒ 但 `unlock.rs` 明说「接线卡知道该填哪个」⇒ 暗示该填）⇒ **两条路都动契约，必须拍** ⇒ 同样阻塞 HTTP 接线卡 |
| **W-FRAME-LEN** | **契约 §5.2 括号写「不含这 4 字节头」，而帧头实为 8 字节** | 字节语义本身无歧义（偏移表 `payload` 在 `[8..)`，且同仓 `kernel-wire-contract.md` §2 的同形帧明写「payload 字节数（不含本头 **8** 字节）」+ 偏移表 `0/4/5/8` 完全同构）⇒ 判定 **`len` = payload 长度、整帧 = 8 + len** ⇒ W3a 据此实现；但**契约文字应订正一处措辞**（⛔ 本轮未改任何契约字节定义）|
| **W-VEC-SIZE** | **W3a 固定向量文件 273 KB**（其中 262 KB 是 `len = 65536` 那一条的 `payload_hex` + `frame_hex` 字面量，单行最长 131,110 字符）| 任务书 §2.6 要求「合法向量给出期望的**整帧 hex**」且把 `len = 65536` 列为边界例子 ⇒ W3a **照字面执行**（同仓已有 195 KB 的 `wg_padding_vectors.txt` 先例，量级相当）。⚠️ 但该文件是**跨仓共享产物**（客户端 → 扩展 → 服务端三仓各存一份）⇒ 273 KB × 3 仓 + `git diff` 不可读。**卡未自行改动**（正确）。⇒ 备选：把该条改成「`header_hex` + `payload_fill_byte` + `payload_fill_len` + `frame_len`」的填充描述（等价、无歧义、任何语言 5 行可复算），文件降到约 **11 KB**；代价是该条不再有字面量 `frame_hex`。**不影响字节语义**，纯载体选择 ⇒ 请主理人定 |

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
| **M-B** | **超出允许清单的改动**（新增 4 个源文件 + 2 个 BUILD.gn + overlay + 补丁 + gtest 脚本），需追认 |
| **M-C** | `computername` 两侧**不完全同源**（内核 `gethostname()` vs 客户端 `HOSTNAME` 环境变量→`/etc/hostname`）⇒ 容器里或不经 shell 启动时两侧会不一致 |
| **M-D** | 基线里 3 条 `test_dispatch_logic` 用例已过期（任务书已拆到 l3h、路线已拍板 4A，用例没跟上）—— 属派卡侧，本卡只报告不改 |
| **M-E** | `integration/kernel_pipe_client.cc` 现在 **570 行**（Q 卡时 453），超 400 软目标（未超 700 硬上限）|
| **M-F** | 客户端侧 `collect_hddid()` 有一个内核侧**没有**的 `SYSTEMDRIVE`/`DISKNUMBER` 兜底 ⇒ 扫不到序列号的机器上两侧必然不同，且兜底采的是盘符 |
| **M-G** | **Windows 多盘选哪一块没写**（R1 只覆盖了 POSIX）：内核只读 `PhysicalDrive0`，客户端若扫 `PhysicalDriveN` 再排序，两侧会选到不同的盘 |
| **M-H** | `cpuid` 的 `__cpuid` 折叠必须与 `base::CPU` 逐字节相同，而**取值规则向量组证不了**（表里是硬编码假值）⇒ 需一次真机交叉核对 |
| **M-I** | `os` 在三平台之外两侧不一致（内核 `""` vs 客户端 `std::env::consts::OS` = `"freebsd"`）|
| **M-J** | **macOS 的 `hddid` 格客户端任务书里根本没有**；内核侧无可移植来源 ⇒ 只能是空串（另找 `IOKit` 来源是新卡的量级）|
| **M-L** | `run_l3_gtest.sh` 的仓库路径参数**必须用绝对路径**（传 `.` 会让 `l3_bootstrap`/`l3_provisioning` 因软链环编译失败）—— 既有行为，基线同样挂 |

### ✅ 已裁定（留档，不必再议）

| 来源 | 项 | 裁定 |
|---|---|---|
| Q-B | 管道名 → OS 端点的映射 | 2026-10-21 13:49，主线提交 `b30922f` |
| Q-C | 契约内部的历史矛盾 | 已在主线修好，Q 按修好的版本实现 |
| Q-F | 契约的两处补充 | 2026-10-21 13:49，主线提交 `b30922f` |
| R-A | 内核侧已合入，闸门自复跑全绿 | 见 `docs/l3-reports/task-R-conflicts.md` R-A |
| R-E | 真编译 + accept 全绿，客户端侧也已合入 | 同上 R-E |
| R-F | R 的跨仓向量已成常驻守卫 | 同上 R-F |
| M-K | 机器码取值的两条细则已核对无误：POSIX 的排序规则两侧都是逐字节序；`os` 不带版本号 | 见 `docs/l3-reports/task-M0-conflicts.md` M-K |

## 已并入主线的裁定（不在本文件）

拍板单与逐条状态在 `docs/env-channel-tasklist.md` 的「待主理人拍板」表
（含 10 条）与「🔴 拍板单」段。
