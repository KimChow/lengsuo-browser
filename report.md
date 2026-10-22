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
| ~~**W-EXT-ID**~~ | ✅ **已裁定（2026-10-22）**：扩展 ID = **`ejlmmjamfpkoopjachjhcbdjlddknhok`**（32 字符 `a`–`p`；旧值 `SbzJDF-u75AnlyE5` **作废**）。派生算法 = `hex(SHA256(公钥字节)[0..16])` 逐位映射 `'a'+值`。manifest **必须注入 `key` 字段**（公钥 PEM）⇒ 否则 Chromium 退回按路径派生 ⇒ ID 随目录布局漂移。**扩展仓已加字段 + 钉死测试（19 测全绿）+ 契约 §5.0 已改** ⇒ **不再阻塞** |
| ~~**W-EXT-DIRNAME**~~ | ✅ **已澄清（2026-10-22）**：**不是冲突，是两个命名空间**——**源码目录名** = 扩展仓里的 `credential`（仓库内定位）；**安装目录名** = 内核包里的 `lengsuo-credential`（`--load-extension` 指向它）⇒ 两者本就该不同 ⇒ **各自保持现值，不改** ⇒ **不再阻塞** |
| **W-EXT-SORT** | **扩展仓 JS 的排序口径与契约 §5.5 不一致** | 契约要求 UTF-8 字节序；JS `sort` 用 UTF-16 码元序 ⇒ **BMP 内一致、astral 字符上分叉**（已实测）。当前扩展文件名全 ASCII ⇒ **无实际影响**；将来出现 astral 文件名两侧摘要会不同 ⇒ 详见 `docs/credential-dispatch-plan.md` §8.5 |
| **W-REC-CASE** | **恢复码输入的大小写宽容度** | W2 二层沿用层一 `base32_decode` 口径：**严格大写、不折叠** ⇒ 小写被拒。⚠️ 若要放宽，**客户端侧必须同步改**（`credential-crypto/src/kdf.rs`），否则又是一处跨仓漂移 ⇒ 详见 `docs/credential-dispatch-plan.md` §9.5 #1 |
| **W-REC-RATE** | **恢复码限速参数未定** | 契约与本仓**都没有**任何限速规定（窗口 / 次数 / 锁定时长）⇒ 二层只定义 `RecoveryLimiter` **接口 + 调用方注入**，未内置任何取值 ⇒ 请主理人定实际参数 |
| **W-REC-TBL** | **凭据表新增两个持久化字段** | `PasskeyInfo` 加了 `flags: u16`（**必需** —— 私钥 AAD = `固定头(24) ‖ rp_id`，`flags` 在 `[22..24]` ⇒ 不存它就**重建不出 AAD**、私钥永远解不开）+ `wrapped_dek_recovery: Option<[u8;60]>`（恢复码路径的包裹）⇒ **凭据表结构变了，请复核** |
| **W-REC-CONTRACT** | **契约 §4 缺两条语义** | ① 没写「恢复码解封的是哪一层」（实为**两级解包**：恢复码 → 真 DEK → 私钥明文，**不解环境授权**）；② 没写两个包裹字段的关系（`wrapped_dek_server` = 真 DEK 被**服务端主密钥**包裹；`wrapped_dek_recovery` = **同一个真 DEK** 被**恢复码派生的 DEK** 包裹、AAD 为**空** ⇒ 两条路径拿到的是**同一把 DEK**，不是两把）⇒ 请补进契约（契约是唯一真相） |
| **W-POL-AUDIT-ID** | **`device_id` 原文入审计**（W2 三层卡自报「取记原文」并请复核） | 审计要能追「哪台机器」，但 `device_id` 是设备标识 ⇒ 请复核**审计留存期 / 可见范围**是否与该敏感级别匹配。卡侧报告 §5 已显式声明此选择 |
| **W-POL-ERRCODE** | **`PolicyError::AuditFailed` 新增变体未接错误码** | 变体是 fail-closed 的必要载体（否则「审计写失败」与「准入被拒」无法区分）；⭐ 但**本卡未碰 `src/error.rs`**（在卡边界外）⇒ **HTTP 层暂时无法把它映射成错误码** ⇒ 需后续接线卡处理。⇒ 请确认该变体是否需带载荷（失败原因 / `StoreError`） |
| **W-POL-ERRTYPE** | **`SecretError`（存储层）被塞进 `AdmissionDeny`（准入拒绝原因）** | 类型上「准入被拒」与「存储故障」有点混；当前靠**不同变体 + 不同审计 `outcome` 标签**区分（`store_error` vs 其它）⇒ 若上层要求「拒绝原因统一是业务语义」，需换成独立新类型 |
| **W-POL-POOLVER** | **`pool_version` 语义（版本号相等 ⇒ 同具体值）的证明责任** | 本仓**没有值池表**（契约 `04-fingerprint.md` / `02-environment.md` 状态均为「**提案，待签核**」）⇒ 卡**只做相等比较**，不解释「同版本 ⇒ 同值」⇒ 该前提应由**契约**负责 |
| ~~**W-NM-ENVELOPE**~~ | ✅ **已裁定（2026-10-22）**：JSON 信封 = **`{"frame_b64": "<base64(整条业务帧)>"}`**（契约 §5.2.1）。理由：① 线上**只有一种**业务帧格式 ⇒ **已合入测过的 W3a 代码就是线上路径**；② 类型/长度**只在**帧头里 ⇒ 无重复字段、无两个真值源；③ 载荷是凭据 ⇒ base64 让它**不可能被误读成 JSON 结构**；④ 信封**永不需改** ⇒ 扩展与宿主在信封层解耦。⛔ 否掉 `{"type":N,"payload_b64":…}`（类型重复）与 `{"type":N,"payload":<json>}`（线上变成第二种格式 ⇒ W3a 白测）。**尺寸**：`kMaximumNativeMessageSize = 1 MiB` 作用于宿主→浏览器方向的 JSON 文本 ⇒ base64 使有效载荷上限约 786 KB ⇒ 实际无影响（凭据消息几百字节）⇒ **不再阻塞 W3b-2** |
| ~~**W-HTTP-DEVID**~~ | ✅ **已解决（2026-10-22，接线卡 `1ee1bc8` 已实现，本轮核实）**：`device_id` **由请求体携带**，与 `handle_exchange` 的 `ExchangeBody{device_id,...}` 完全同构；`tenant_id`/`user_id` **只**来自登录态（请求体塞这两个字段被 `deny_unknown_fields` 拒 ⇒ 10002）。设备绑定由 `DeviceBindings::is_bound` 在 `unlock_secret` 闸 3 判。⇒ 契约 §7.5「登录态 + 设备绑定」的正确落地形态 = **登录态给 tenant/user，请求体给 device_id，再用绑定边校验**。✅ 契约 §7.6 已同步该口径 |
| ~~**W-HTTP-PERM**~~ | ✅ **已解决（2026-10-22）**：**不新增权限码**。`tests/scope.rs:119-144` 明文钉死「本仓只允许 `REMOTE_SCOPE_PERMISSIONS`(3) + `DEVICE_PERMISSIONS`(4) 两个集合，第三个域的码会让该测试红」⇒ 加 `credential:unlock` 属**契约级扩宽**，本轮不擅自做。授权改由 **`store_auth`（成员 ⇄ 环境）** 承担 —— 与对标端一致：对标端 `passkey_list:7` / `hasPasswordViewAuth` 是「看不看得到」，而**成员能不能操作这个环境**由 `store_auth(store_id,user_id,granted_by)` 判（`account-sharing.md` §12、`enterprise-model.md` §三）。⇒ 审计 `permission` 维持**留空**（本仓权限码集合封闭，凭据域无动作码）|
| **W-UNLOCK-OWNER** ⭐ | **已发现并修复（2026-10-22）**：接线卡原判据 `secret.account_id == auth.user_id` 是**跨命名空间比较** —— `SecretRef.account_id` / `PasskeyRef.account_id` 的语义是「**店铺账号**」（`passkey-decision-items.md` §5.6 逐字「哪个店铺账号用哪个 passkey」），而 `auth.user_id` 是**登录用户**。生产上二者永不相等 ⇒ **解锁恒被拒** ⇒ 「同一个公司下面的都可以使用得到，只要给权限」**走不通**。⇒ 修为 `ensure_store_authorized(auth, store_id)`（成员 ⇄ 环境，新增 `StoreAuthorization` trait，与 `DeviceBindings` 同构、fail-closed）。服务端线 `1d05c32` + 契约 §7.6 已改；47 上门禁全绿（含 11 条跨仓 interop 字节互验）|
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
