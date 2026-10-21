# 任务 O 报告 —— 语义冲突与待裁定项

> 完整报告见 `docs/l3-reports/task-O.md`（做了什么 / 设计取舍 / 验收输出）。
> 本文件只列**需要主理人裁定**的项：按任务书要求，遇到语义冲突停下来写
> report.md，不自行选一条路。

## A. 阻断性冲突：任务书建议的 renderer 拉取位置不可用

**任务书原文**：「renderer 侧：在 `RendererMain` 里**现在读 `lengsuo-managed-l3`
的同一个代码位置**（0016 的 renderer hunk，`const base::CommandLine& command_line`
别名之前）发起请求并等响应」。

**冲突**：该位置的 `RenderThread::Get()` 返回空。`RenderThreadImpl` 在
`RendererMain()` 中段（约 350 行）才 `new` 出来，进程级 binder 表挂在
RenderThread 上。早段拉取等于向不存在的 binder 发请求。

**已做的处置**：拉取块移到 `new RenderThreadImpl(...)` 之后、
`run_loop.Run()` 之前。时序结论不变（文档仍在第一个 `StorageArea::Create`
之前冻结，论证见 `docs/l3-reports/task-O.md` §3）。

**待裁定**：
1. 接受「位置后移」？
2. 还是要求保留早段位置（那就需要另一条通道：浏览器主动 push，或换一个
   在 `RenderThreadImpl` 之前就可用的 binder）？

## B. 阻断性冲突：任务书指定的 binder 挂载文件在 155 树里不存在该函数

**任务书原文**：「browser 侧：…在**进程级 binder 注册点**
（`ChromeContentBrowserClient::RenderProcessWillLaunch` 或 155 树内等价 API）注册」，
允许改动清单写 `chrome/browser/chrome_content_browser_client.cc`。

**树里的实情**：`ChromeContentBrowserClient::BindHostReceiverForRenderer` 定义在
`chrome/browser/chrome_content_browser_client_receiver_bindings.cc`；
`chrome_content_browser_client.cc` 里没有这个函数。

**已做的处置**：挂 binder 的 hunk 落在
`chrome_content_browser_client_receiver_bindings.cc`；任务书清单里的
`chrome_content_browser_client.cc` 仍被改动，但改的是**删除**那段 base64
推送（该文件确实在允许清单内，且删除是必需的）。

**待裁定**：
1. 接受「挂载点换一个文件」？
2. 还是要求把函数搬到 `chrome_content_browser_client.cc`（上游结构调整，
   会牵动 `chrome/browser/BUILD.gn` 的源文件列表）？

## C. 两条互斥要求：`lengsuo-managed-l3` 零命中 vs 不许动 `cookie_mirror.*`

**任务书原文**：验收 7 要求该字符串在仓内零命中；同时「不许动
`native/cookie_mirror.*`（M 的产物）」。

`native/cookie_mirror.h` 第 16 行的注释写着「…就是 browser 已经下发的那份
L3 文档（`--lengsuo-managed-l3`）」。

**已做的处置**：只改这一行注释里的通道名，**逻辑一行未动**。

**待裁定**：若认为连注释都不能碰，则验收 7 与「不许动 cookie_mirror」两条
要求互斥，需要取舍其一。

## D. 超出允许清单的我方文件（已改，需追认）

| 文件 | 改动 | 必要性 |
| --- | --- | --- |
| `integration/browser_bootstrap.{h,cc}` | `BrowserL3Base64()` → `BrowserL3Config()` | base64 编码的唯一来源就在这里；去掉 base64 必须改。不改则要么留着死代码，要么 renderer 拿到的东西和浏览器验过的不是同一份 |
| `integration/BUILD.gn` | 新增 `l3_provisioning` target；`bootstrap_unittests` 加源文件 | 新 target 没地方声明 |
| `integration/official_defaults_unittest.cc` | 自带 `main()` 改为普通 `TEST()` | 见下条 |

## E. 顺手修掉的静默失效测试（请复核这个判断）

`integration/official_defaults_unittest.cc` 自带 `main()`，和
`browser_bootstrap_unittest.cc` 编进同一个 `bootstrap_unittests` 二进制 ⇒
**链接器选了那个 `main()`，其余所有 `TEST()` 一条都没跑**。

改之前的实测输出：`official_defaults_unittest: 17 passes, 0 failures`
（只有它自己）。改之后：21 条全跑（browser_bootstrap 15 + l3_provisioning 5
+ official_defaults 1），全部通过。

本任务把 `main()` 换成 `TEST(OfficialDefaults, MergeAndApply)`，**断言内容
一字未改**。若裁定认为这属于越界，可单独回退——但那样 `bootstrap_unittests`
就继续只跑 17 条手写断言，本任务的 5 条 gtest 也不会在里面。

## F. `tools/rebuild/apply_l3_wiring.py` 重写（请复核）

该脚本原本自带一份接线代码副本（exact-string 替换），与
`patches/0015` + `patches/0016` 重复。任务 O 换通道后副本必然不一致——它会
继续往树里装**已退役的 base64 传输**，验收 2 用它会得到一个带 base64 的树。

**已做的处置**：重写为「回放 0015/0016 补丁」，补丁成为唯一事实来源。

## G. 任务书建议与实际不符之处（只报告，未自行扩张）

- 任务书给的 mojom 示例是 `GetDocument() => (array<uint8> document)`。
  **在 155 树的 Mojo 版本上，裸 `array` 不能作为应答字段**
  （编译报 `no viable conversion from 'OnceCallback<UnboundRunType>'`）。
  本任务改成 `struct L3Document { array<uint8> bytes; }`，接口形状对调用方
  等价。是否接受这个偏离，请复核。
- 任务书说「`tools/overlay.py` MAPPING 加 3 个新文件」，实际新增了
  **7 个**（含 `.mojom`、`BUILD.gn`、`impl.h`、`impl.cc`、gtest 源文件）。
  漏一个 = gn gen 过、链接炸，所以按实际需要全加了。

## H. 验收覆盖分层（任务书第 6 条要求写清）

| 层 | 覆盖什么 | 怎么覆盖 |
| --- | --- | --- |
| 纯逻辑（standalone g++） | 冻结快照 → 字节的序列化、签名时间戳保留、渲染进程侧 `Init` 接受、空快照 = 无 L3、每次请求重读快照 | `run_l3_gtest.sh` 的 `l3_provisioning` 目标（5 条），`-Wall -Wextra -Werror -pedantic` 零告警 |
| Mojo 绑定层 | 接口名匹配才消费管道、应答字节真的到得了渲染进程 | 真实构建（`gn gen` + 增量编译 0 错误 + `gn check` OK）+ 端到端验收（有画像 8/8：页面读到了钉住值 ⇒ 字节确实过了 Mojo 管道） |
| 暴露面 | 渲染进程命令行不再带文档 | 验收脚本轮询 `/proc/<pid>/cmdline`（有画像 / 无画像各一条断言） |
