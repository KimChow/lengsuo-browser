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
| **凭据线 W** | `docs/credential-dispatch-plan.md` §8.3（W6/扩展 ID）/ §8.5（W5 派卡实录）/ **§20（host manifest 注册卡）** | （同左；均为派卡方取证，非卡侧报告） |
| **T3**（0019，接线）| `docs/l3-reports/task-T3-wiring-conflicts.md` | `docs/l3-reports/task-T3-wiring.md` |

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
| ~~**W-EXT-ID-VS-PUBKEY**~~ ⭐ | ✅ **已事实解除（2026-10-23 08:3x 复核）——原记录是旧 clone 造成的假警** | 原记「扩展仓公钥派生出 `elmafkojcjcdldno`/`jmjmpopahhbjdkhk`，不等于 `ejlmm…`」。**实测**：47 上扩展仓已 `ff-only` 到 **`a61fcef`**（= `origin/main`，**已推 GitHub**），取 `extensions/credential/manifest.json` 的 `key`（450 字符 PEM）→ 剥头尾 base64 解码得 DER **294 字节**（`30820122300d…`）→ `SHA256(DER)` = `49bcc90c5faeef9027972139b33ad7ea8fb4c19c02e62c903d9617f5c67cabc2`（**与契约 §5.0 记的一致**）→ 按「每位半字节映射 `'a'+值`」派生 = **`ejlmmjamfpkoopjachjhcbdjlddknhok`**，**与契约逐字符相同**。⇒ **无需任何修正、无需裁定**；§14.2 当时用的是 `d113888` 旧 clone（§14.2 自己就写了「clone 会落后 ⇒ 必须先 `git pull --ff-only`」）。⚠️ **可复用判据：跨仓值比对前必须先 `git pull --ff-only` 并核对 HEAD == origin/main**。 |
| **W8-UNLOCK-BATCH** ⛔ | **W8「`cred_refs` 批量解锁」与服务端唯一解明文路径「只解一条」直接冲突** | W2 已合入的服务端在**三处**独立写死「无批量/列表/导出」：`crates/lengsuo-coord/src/secret/unlock.rs:58`「本模块不提供任何「批量 / 列表 / 导出」明文的接口 ⇒ 公开 API 只有「解一条」」；`src/http/unlock.rs:25`「**只解一条、只回一条**（契约 04 §4：「无『列表导出明文』类接口」）」；`docs/server-contract/04-fingerprint.md:37`「返回即用即弃；无「列表导出明文」类接口」。而 W8 在派卡计划里写作「凭据灌注进环境启动流程（**`cred_refs` 批量解锁**）」。⚠️ 同时 **`cred_refs` 在 `credential-wire-contract.md` 里没有任何定义**（全文仅出现在派卡计划 W8 行）⇒ 既是**语义冲突**又是**未冻结的跨仓格式**。三条候选路（⛔ 均需主理人裁定，本卡未自行择一）：① **W8 保持「逐条 unlock」**：环境启动时对 `cred_refs` 逐个调 `POST /client/secrets/unlock`（N 次往返、N 条审计、每条即用即弃），把「批量」理解成「启动时循环多次」而非一个批量接口 ⇒ **不动服务端、不动契约**，代价是 N 次网络往返；② **新增真正的批量接口**：改服务端 + 改契约 04 §4 的「无列表导出」条款 + 定义 `cred_refs` 字节格式 ⇒ **跨仓变更、需三仓同步**，且与「即用即弃」安全口径张力大；③ **W8 拆两步**：先只做「环境侧 `cred_refs` 解析 + 单条注入管线」（不碰服务端、不定义批量协议），批量语义另行开卡。⇒ ✅ **2026-10-24 已按路径①派出并合入**：主理人 00:20 复核判定「W8 可派」但未指定 ①②③ ⇒ 派卡方取「**契约自身唯一明文出口**」这条读法，按①派出 `w8-credential-env-injection`（逐条 unlock、不新增接口、不改服务端、不改契约；⛔ 未自行发明跨仓字节）。**⇒ 卡 `2a498607` → 合流 `64a7c5b3`，GitHub `main` = `68085be7`（已推）；六道门禁全 0 + 派卡方在合流后 main 上独立复跑 16/16 凭据用例全绿** ⇒ **服务端与契约零改动**，语义冲突已以「启动时循环逐条」消解。详见 `docs/credential-dispatch-plan.md` §18 / §19，另见下三条新登记缺口（仍待裁定）。 |
| **W8-CRED-REFS-FORMAT** ⛔ | **`cred_refs` 的跨仓字段面 / JSON 键名 / 字节格式未冻结** | 契约 `credential-wire-contract.md` 全文 grep **零命中**；服务端契约 04 §4 只有散文「对外只发 `cred_ref`（**引用 id + 过期 + 作用域**）」⇒ **既是未冻结的跨仓格式，也是本轮 W8 的第一顺位缺口**。⇒ W8 卡的做法：**只定义进程内类型**（引用 id 及其形态校验），⛔ 不定义 JSON 键名 / 不定义批量协议 / **不做任何反序列化**；缺口原样上报，等主理人补契约。 |
| **W8-HTTP-TRANSPORT** ⛔ | **客户端仓没有任何服务端 HTTP 客户端 ⇒ 解锁没有传输实现** | 实测：`lengsuo-network` 仓 `Cargo.lock` **无** `reqwest`/`hyper`/`ureq`/`isahc`；`reqwest = "0.12"` 只写在根 `Cargo.toml` 的 `[workspace.dependencies]` 里而**没有任何 crate 使用它 ⇒ 未进 `Cargo.lock`** ⇒ 一旦有 crate 引用就要解析整棵新依赖树（hyper 等），而**派卡器不设代理**（计划 §7.1）⇒ 新依赖必须**派卡前**先装进 `~/.cargo/registry`。另：`crates/` 内 grep `cred_ref\|secrets/unlock\|SecretRef` **零命中** ⇒ 该仓对服务端凭据域完全陌生。⇒ W8 卡的做法：解锁以 **trait 注入**（泛型参数，⛔ 不用 `dyn`——仓里没有 `async-trait`，AFIT 不做 dyn 兼容），**零新依赖**；真实 HTTP 接线留给后续接线卡。 |
| **W8-LEASE-UPSTREAM** ⛔ | **提供 `cred_refs` 的服务端端点未实现，其字段面规格尚未签核** | `POST /client/fingerprint/lease` 的响应含 `cred_refs`（服务端契约 04 §2），但该端点**未实现**；其字段面冻结草案 `lengsuo-client/docs/fingerprint-profile-spec-v1-freeze.md` 明写「**提交主理人签核。签核前不派任何指纹端点卡。**」且把 lease 列为「签核后立即能派」⇒ **上下游都还没到位**。⇒ W8 卡的做法：**不依赖**该端点（引用列表由调用方给出）。 |
| **W-MSG-PAYLOAD** ⛔ | **契约 §5.3 只定「类型编号 + 方向 + 含义」，未定 payload 字节格式** | W4a（通道层 `w4-extension-native-bridge`）可按「payload 不透明」交付；但 **W4b（页面劫持 + 密码/TOTP 注入）与 W7（填充脚本）无法开工** —— 要构造「取密码」「passkey create/get」请求，必须先知道 payload 里放哪些字段，而契约 §5.2 只规定 `payload` 是 `[8..)` 的字节、`len` 是它的长度，**没有任何字段定义**。⇒ **建议新增契约 §5.3.1「payload 编码（v1）」**：载荷 = **UTF-8 JSON**，每类型请求/响应字段如下（**草案，待主理人拍板**，拍板后写进契约并三仓同步）：<br>`0 列举` `{nonce}` → `{items:[{id,rp_id,kind}]}`<br>`1 取密码` `{nonce,origin,username?}` → `{password}`<br>`2 取 TOTP` `{nonce,ref}` → `{code,expires_in}`<br>`3 passkey create` `{nonce,rp_id,challenge,user:{id,name,display_name}}` → `{credential_id,cose_key_b64,auth_data_b64,signature_b64}`<br>`4 passkey get` `{nonce,rp_id,challenge,allow_ids[]}` → `{credential_id,auth_data_b64,signature_b64,user_handle?}`<br>`5 保存密码` `{nonce,origin,username,password}` → `{ok}`<br>`6 风控预检` `{nonce,origin}` → `{decision}`<br>`7 撤销引用` `{nonce,ref}` → `{ok}`<br>`8 恢复码` `{nonce,recovery_code}` → `{ok}`<br>`9 授权查询` `{nonce,store_id}` → `{granted}`<br>`10 UseNativeResponse` `{}`（**仅宿主→扩展**，无载荷）<br>⛔ 派卡方**未自行定义**（契约是跨仓唯一真相，硬规则 #4）⇒ 选定前 **W4b / W7 不派**。⭐ **2026-10-24 09:3x ✅ 已冻结（主理人授权「自己逆向处理」）**：契约新增 **§5.3.1「payload 编码（v1）」**——载荷 = UTF-8 JSON；每条请求必带 `nonce`、响应回显；未知字段宿主忽略；10 类请求/响应字段表（0 列举 / 1 取密码 / 2 取 TOTP / 3 create / 4 get / 5 保存密码 / 6 风控预检 / 7 撤销引用 / 8 恢复码 / 9 授权查询）+ type 10 `UseNativeResponse`（宿主→扩展，无载荷）；§6 加检查第 12 条。**依据是竞品实测协议**：`docs/passkey-cross-device-reverse.md` §5.4–5.5（竞品 `fido2_content.js` 反编译）确认回退三态、type 10 编号、create/get 响应字段集（credential_id / auth_data / signature）；密码/TOTP 字段为我方自有（竞品无此类消息），⛔ 不强行对齐竞品编号。⇒ **W4b / W7 解除阻塞**。⭐ **2026-10-24 09:5x W4b 已合入 `53f5317`**（已推 GitHub；7 文件；26 新测试 ⇒ 6 文件 **75/75**；页面脚本 391 行；nonce/origin/challenge 三校验 `:147/:151/:154`、`Object.setPrototypeOf` `:202`、手写 CBOR `:159-174`、SW `policy_denied` `:39` + `TextEncoder(JSON.stringify(payload))` `:93`、内容脚本 `origin: location.origin`）⇒ **托管链路实现侧打通**；后续：宿主侧 `dispatch`（等 W3b-2d 合行，同 crate）、**W7 可派** |
| **W5b-INSTALLER-UDD** 🟡 | **契约 §5.4 给的 `install_host_manifest(install_root, platform, runner)` 三参数在 POSIX 上不可实现** | §5.4 同节又写 POSIX 的 `user_data_dir` 由启动方传入（§3.2 `plan_posix`），而三参数里没有它 ⇒ 卡 `w5b-install-host-manifest`（2026-10-24 10:3x 派出，base `4e9e3742`，slot 303）改用结构体 `HostInstallRequest { install_root, platform, user_data_dir }`，并在报告里原样上报（⛔ 未改契约文档）。另三个**自设工程决定**同卡上报、待复核：① Windows 上 manifest JSON 文件本身的位置 = `<install_root>\lengsuo.credential.json`（§5.4 只定它是注册表默认值的数据）；② 宿主二进制存在性 fail-closed 校验（与 `BrowserBinary::locate` 同口径）；③ `description` 取值 `Lengsuo credential native messaging host`。⇒ **请裁定是否把第四参数补进契约 §5.4**（补则三仓同步；不补则卡内实现即为准）。 |
| ~~**W-LINE2-EXT**~~ | ✅ **已解除（2026-10-23 19:2x，主理人授权「目录不存在你就建好」）**：① 派卡根 `~/lengsuo-extension-dev` **已建成**；② `bin/` 放闸门脚本（`card_merge`/`card_guard`/`card_rules`/`card_ledger`）+ `config.json` 四道 **node** 门禁（行数 / docs / `node build/build.mjs` / `node --test`）⇒ **扩展仓 repo 零改动**；③ 基线实测四道全绿（16/16 测试）。⇒ W4a / W4a-2 已合入（`acb67c2` / `ea4fc3d`）⇒ **「没有线」不再是阻塞**。⚠️ **W4b / W7 的真实剩余阻塞已换为 `W-MSG-PAYLOAD`**（契约 §5.3 未定 payload 字节，见上一行）⇒ 详见 `docs/credential-dispatch-plan.md` §17 / §4 |
| ~~**W-EXT-DIRNAME**~~ | ✅ **已澄清（2026-10-22）**：**不是冲突，是两个命名空间**——**源码目录名** = 扩展仓里的 `credential`（仓库内定位）；**安装目录名** = 内核包里的 `lengsuo-credential`（`--load-extension` 指向它）⇒ 两者本就该不同 ⇒ **各自保持现值，不改** ⇒ **不再阻塞** |
| **W-EXT-SORT** ⭐ | **扩展仓 JS 的排序口径与契约 §5.5 不一致（2026-10-24 06:1x 实跑确证：共享向量已能区分）** | 契约 §5.5 要求「路径按**字节序**排序」；其 `rules` 进一步明文「sort by UTF-8 byte order (NOT locale, **NOT UTF-16 code unit order**)」；而 JS `build.mjs` 的 `out.sort((a,b)=>(a<b?-1:a>b?1:0))` 按 **UTF-16 码元序**。⭐ **本轮实跑**：取 Rust 侧共享向量 `crates/lengsuo-browser-package/tests/vectors/extension-digest-v1.json` 的 `astral_unicode`（文件名 `U+E000` + `U+1F600`）在 47 上实跑 JS `digestDirectory` ⇒ 得 `aaa9b71d…`，**≠ 向量期望值 `25c145…`**（同批 `bmp_unicode` 通过）⇒ **契约 §6 #9「Rust 与 JS 两份实现用同一组固定向量产出相同结果」在 JS 侧事实上未被强制**（扩展仓**未携带**该向量文件；`digest.test.mjs` 仅 4 条**合成 ASCII** 用例 —— 其注释自称 "The vectors here are the contract" 与事实不符）。⚠️ **当前无生产影响**（扩展目录文件名全 ASCII，dist 实测 9 文件，摘要 `865c7716…` 与独立 Python 复算一致）；⛔ 但**不修即长期漂移**：一旦扩展出现 astral 文件名，两侧摘要分叉 ⇒ 内核包 `extension_sha256` 校验必失败。**建议修法（需主理人批一句即派卡）**：① JS 改按 UTF-8 字节序排（`Buffer.compare(Buffer.from(a,'utf8'), Buffer.from(b,'utf8'))`）；② 扩展仓携带**同一份** `extension-digest-v1.json` 并加 astral/byte-order 回归用例（先红后绿）。⛔ **契约字节定义不动**（本轮未改任何契约字节）。详见 `docs/credential-dispatch-plan.md` §8.5 / §20。⭐ **2026-10-24 09:0x 追加（修法被门禁实测挡住）**：扩展线 `card_rules.py` 的 `SHARED_PATHS` 含 `build` ⇒ 「改 `build/build.mjs` + 加测试」的卡在 check 阶段被拒（用临时 spec 实测：`✗ 共享路径必须独占：['build/build.mjs']`）；拆成实现卡 + 测试卡又违反「测试先失败再实现」⇒ 修法需主理人三选一（一次性例外 / digest 移出 `build/` / 暂不修），见计划 §21.3。⭐ **2026-10-24 09:3x ✅ 已解（不动门禁）**：单文件基建卡 **`w5-extension-digest-byte-order`**——`touches` **只有 `build/build.mjs`**（`len(scope)==len(hits)` ⇒ 门禁实测 `✓ 可以派卡`），回归**自检写进 `build.mjs`**（`digestOrderingSelfCheck()` 在 `main()` 开头，门禁 `node build/build.mjs` 即测试）；夹具/期望与 Rust 向量 `astral_unicode` **逐字节相同**（`\uE000.txt`="x" + `\u{1F600}.txt`="e" ⇒ **`25c145…`**；现状 UTF-16 码元序 = `aaa9b71d…`，独立 Python 复算确认）。在跑 base `ea4fc3d` |
| **W-NM-HOST-MANIFEST** ⭐ | **W3b-2b 渲染出的 host manifest 过不了 Chromium 的解析器，且没有任何代码把它装到 Chromium 会读的位置**（已发现 ⇒ 已派卡修 G1+G2） | `chrome/browser/extensions/api/messaging/native_messaging_host_manifest.cc` 必填 `name`(`:78`)、`description`(`:85`)、`type` 且必须 `=="stdio"`(`:92-94`)、`path`(`:100`)；而 `crates/lengsuo-native-host/src/manifest.rs` **只渲染 `name`+`allowed_origins`**，且固定向量 `tests/vectors/native-host-io-v1.json` 把这份**不完整输出**钉成了「冻结期望」⇒ Chromium 必拒。同时全仓 grep `NativeMessagingHosts`/注册表写入 = **零命中** ⇒ `launch_context_win.cc:204` 的 `Native messaging host … is not registered.` 必然发生 ⇒ **NB 管道永远建不起来**。⇒ 已派 **`w3-native-host-register`**（2026-10-24 07:3x，客户端线 `credential` lane，base `a1e5213a`）补齐 `description`/`type`/`path` 并实现注册层（Windows 用户级键 / POSIX 用户目录，注入执行器，零新依赖）。⛔ **不动契约** —— host manifest 的字段集**由 Chromium 自己的解析器决定**（上列行号），契约 §5.0 只钉 host 名/扩展 ID/目录名。⇒ ✅ **已修**（`w3-native-host-register` 卡提交 `49655d97` → 合流 `02b8459a` 已推 GitHub：五字段渲染 + 注册层 + 向量改手写五字段期望；`test --workspace` 2805 passed，派卡方独立复核见计划 §20）|
| **W-NM-HOST-BIN** ⛔ | **Native Messaging 宿主可执行文件不存在 ⇒ manifest 的 `path` 无处可指** | `crates/lengsuo-native-host/Cargo.toml` **没有 `[[bin]]`**，仓内唯一 `[[bin]]` 在 `lengsuo-node`；`lengsuo-native-host` 目前是**纯库**（lib + tests）⇒ 上述 manifest 的 `path` 只能指向**空气**。`w3-native-host-register` 已把 `path` 做成**入参**并在报告里上报本缺口（⛔ 不发明内核包布局）。⇒ 🟢 **已派卡（G3 前半）**：`w3-native-host-bin`（2026-10-24 09:0x，base `02b8459a`，touches 4 文件）——`src/main.rs` 自动成为 bin（名 = package 名，零 Cargo 面）+ argv[1]=origin 严格校验（先于任何读）+ 忽略 Windows `--parent-window` + `io_loop::run` 真实 stdio + `CARGO_BIN_EXE` 跨进程测试（stdout 纯度）；业务分派保持 `UnwiredDispatch`。⭐ **2026-10-24 09:4x 卡完成 `18a6f394`**（门禁全 0；4 文件 +421）⇒ ⛔ **合行被拒**：`LedgerError: 工作区不干净` —— `repo` 有 **7 个 `crates/ss-client/*` 未提交改动**（TUN 线，非本卡，rule 0e 不碰）⇒ 主线干净后重跑 runner 即合。⭐ **G3 后半已决（2026-10-24 09:3x）**：契约新增 **§5.4「宿主二进制在内核包中的位置」**——`<install_root>/lengsuo-native-host[.exe]`（与内核二进制同级，`extensions/lengsuo-credential/` 亦同级），manifest `path` = 该绝对路径、安装时写入（Windows 注册表 / POSIX `<user_data_dir>/NativeMessagingHosts/lengsuo.credential.json`）。依据：`lengsuo-browser-launch/src/command.rs:83` 原样追加 `--load-extension=`、`spec.rs::BrowserBinary::locate` fail-closed、安装器库注释「只校验不解压」⇒ 目录布局方案 ③。⇒ **安装器卡（`install_host_manifest`）可派** |
| **W-NM-REGKEY-BRANDING** | **Windows 注册表键的品牌分支未取证**（注册卡默认只写 Chromium 键） | `launch_context_win.cc:66` 的 `SOFTWARE\Chromium\NativeMessagingHosts` 受 `:65` **`BUILDFLAG(CHROMIUM_BRANDING)`** 门控，`:78` 的回退键是 Chromium 源码原文的 `SOFTWARE\Google\Chrome\NativeMessagingHosts` ⇒ **我方自建内核（非品牌构建）是否定义 `CHROMIUM_BRANDING` 尚未取证**。注册卡按「只写 Chromium 键」实现（理由：不污染用户真实安装的 Chrome）⇒ **待取证/待追认**；另注：Chromium 的 host manifest **查找不读扩展 ID**（`allowed_origins` 由 Chromium 自己判），故本项与 `W-EXT-ID` 无关。⇒ ✅ **已取证关闭（2026-10-24 08:4x，注册卡方案正确）**：内核真实构建产物 `src/out/lengsuo/gen/build/branding_buildflags.h` 写明 `BUILDFLAG_INTERNAL_CHROMIUM_BRANDING() (1)`、`GOOGLE_CHROME_BRANDING() (0)`（args.gn `is_official_build=false`）⇒ Chromium 品牌键分支**被编译进了内核**，只写 `HKCU\SOFTWARE\Chromium\NativeMessagingHosts` 会被本内核命中；Google 键不写（不污染真 Chrome） |
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
| **T3-A** ⛔ | **目标值入口无驱动方，且「接到既有注入点」接线卡做不到** | `SetFpTargetsFromBundle()`（`lengsuo/native/fp_overrides.h:59`）是唯一「按 URL 取目标值」入口，全量补丁树里**零调用方**（只有本卡新写的 gtest）。目标表的数据源是 ConfigBundle 的 `fp_target` 段，**冻结在浏览器进程**（`lengsuo/integration/kernel_pipe_client.cc:481-491`，`FrameType::kConfigBundle`），**没有任何路径转发给渲染进程**。现有两个注入点：① `--lengsuo-managed-fp` 开关（`renderer_main.cc:185` ← `chrome_content_browser_client.cc:2944`）形态是**定长冒号字段**，装不下变长目标表；② L3 Mojo 通道（`renderer_main.cc:359`/`:383` `GetL3Runtime().Init(l3_wire,…)`）—— L3W1 文档**没有指纹目标段**（`l3_wire.h` 的 `L3Config` 只有 `pins`/`extensions`/`rules`/`cookies`/`sign`/`signed_unix`）。两处宿主文件（`content/renderer/renderer_main.cc`、`lengsuo/native/l3_runtime.*`、`lengsuo/public/mojom/l3_provisioning.mojom`）**全属 patch 0016**（⛔ 本卡不许改），而「自造第二套通道」被必做项 1 明文禁止 ⇒ 必做项 1 的退路**走不通**。⭐ 四条候选路（①扩命令行开关 ②给 L3W1 加段 ③另开接线卡 ④新增专用开关——④已因必做项 6 排除），**本卡未择一**。无论走①还是②，本卡交付的入口**都不用改**（与 `InitFromSwitch()` 同构：一份值 → 内部装状态）。详见 `docs/l3-reports/task-T3-wiring-conflicts.md` T3-A |
| **T3-B** ⛔ | **任务书点名的出口无法被目标值驱动** | 任务书要求把扰动喂给「现有的 `WebGLVendor` / `AudioSeed` / `PerturbGlyphAdvance` 等出口」。但：`WebGLVendor()`/`WebGLRenderer()` 是 **seed+slot 解出的字符串面**，而 `FpTarget` 只带不透明的 `target_hash` + 采样几何（`width`/`length`/`drew_text`）⇒ **没有 vendor 字符串字段**，「目标 vendor 是谁」无从定义；`AudioSeed()` / `PerturbGlyphAdvance()` 是**画像级**标量，而 `enum class FpDimension : std::uint8_t { kCanvas, kWebgl }`（`native/fp_sampling.h:38`）**没有 audio/font 维度**。⇒ 本卡**只接了唯一自洽的那条**：`WebGLParamBias(pname, original)`（按 pname 的 `kWebgl` 出口，patch 0008 在 `webgl_rendering_context_base.cc` 调用），并复用**同一段**候选选择代码（原函数体挪成 `BiasWithSeed(seed,…)`，两个出口分别喂 `WebGLParamsSeed()` 与目标 seed）。三条候选路（①扩 `FpDimension`+让 target 带面字符串 ②只接 webgl 参数出口并上报缺口 ③给 `FpTarget` 加 `vendor`/`renderer` 字段），**本卡取了②，需追认**。详见 conflicts T3-B |
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
| T3-C | 画布维度的消费端 `native/canvas_apply.cc`（patch 0007）在允许清单外；`fp_overrides.cc` 的 `g_canvas` 是**算了从没被读过**的死值 ⇒ 本卡对画布**只交付查询入口** |
| T3-D | `navigator_concurrent_hardware.cc` / `navigator_device_memory.cc` **有意未动**（任务书写「只在必要处」）：它们驱动的是画像级标量 `HardwareConcurrency()`/`DeviceMemoryGB()`，`FpDimension` 无对应维度 ⇒ 无 URL 可传 |
| T3-E | 「**目标优先于画像槽位**」（`fp_overrides.h:56-58`）是本卡对必做项 2 的读法 ⇒ 推论「画像槽位关着时目标仍生效」（单测已钉死）。任务书未明说两者并存时谁赢，另一条读法结论相反 ⇒ 请复核 |
| T3-F | 必做项 4（按既有 `CanvasSample`/`WebGLSample` action 回采上报）**本卡未做**：上行发送端在上行链路（patch 0016/0017 范围）不在允许清单内；且无驱动方 ⇒ 无采样可报（必做项 5 要求默认关闭时上报一次都不发生）|
| T3-G | 两个文件**超出允许清单**（`native/fp_overrides_test.cc` 新建、`tools/rebuild/run_l3_gtest.sh` 加 TARGETS 条目）⇒ **需追认**。前者是任务书 §验收要求的新单测载体；后者不加则新单测根本不会被编译运行（与 O-D / Q-E / N-G 同类）|
| T3-H | **GN 依赖图改动需真实构建确认**：为免成环拆出新叶子 `canvas_noise_algorithm`，`fp_sampling` 改指它，`canvas_noise` 依赖 `:fp_target`。⛔ 本卡不许跑真实构建 ⇒ 只在图上推演过。⚠️ 连带：`fp_target`→`kernel_pipe_json`+`profile_bundle` 现**经由 `canvas_noise` 进 blink**，若某 blink target 不能依赖这两个则需换形态 |
| T3-I | `native/fp_overrides.cc` 401 → **479 行**（> 400 软目标，< 700 硬上限；hard 失败 0）⇒ 要压回可把「按 URL 取目标值」拆成独立 source_set |

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
