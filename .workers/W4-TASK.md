# W4 任务：language 覆盖点设计（RendererPreferences 链）

你是 lengsuo-browser 内核仓的开发工人。你的分支：`worker/w4-language-design`。
只在本分支提交，不 push，不 merge main。产出为设计文档，不改集成代码
（Chromium 树只读研究）。

## 背景

2026-10-16 已定论（`docs/batch2-screen-dpr-design.md` 末节）：
wire 里已有 `language` 字段（LBPF1，可打印 ASCII，≤35），但
`accept_languages` 位于 **RendererPreferences**（155 的 WebPreferences 无
语言字段），此前尝试 WebPreferences 钩子写入导致编译失败并撤销
（1.47 `reviews/build-b6.log` 为证）。本任务把正确的覆盖点设计定稿。

## 必读

`AGENTS.md`；`docs/batch2-screen-dpr-design.md`（language 定论节）；
`docs/catalog-contract.md`（language 是否依赖 catalog 的边界讨论）；
155 固定树（只读）：`/home/ubuntu/lengsuo-browser-dev/src`。

## 必须在 155 源码核实（文件:行号）

1. `RendererPreferences.accept_language`（content/public/common/renderer_preferences.mojom）
   字段与填充链：谁构造 RendererPreferences
   （`RenderProcessHostImpl`/`RenderFrameHostImpl` PopulateScrollbarRenderingPolicy 附近），
   chrome 层谁填 accept_language（`chrome/browser/ui/webui` /
   `ProfileImpl::GetAcceptLanguages` / `prefs::kAcceptLanguages` 读取点）。
2. renderer 侧消费：`RendererPreferences` 如何进 blink
   (`renderer_preferences.cc` → `WebPreferences`/`blink::WebSettings` 的
   accept_languages? Blink 侧 `AcceptLanguagesEnabled`/`NavigatorLanguage`
   的实际取值链——`blink::WebLocaleList`/`LocaleList`)。
3. 网络侧：`Accept-Language` HTTP 头取值来源（network service 的
   `NetworkContext`/`URLRequestHttpJob` accept_language 设置点，
   devtools accept-language override 的存在说明什么）。
4. 之前编译失败的原因定位：WebPreferences 无该字段，正确注入点是
   RendererPreferences 构造处（browser 侧单点）还是 profile pref 层？
   给出唯一推荐覆盖点 + 备选，论证 JS `navigator.language(s)` 与
   HTTP 头是否同源同覆盖。
5. timezone 同批梳理：wire 已有 timezone 字段（≤64）。ICU/`TZ` 环境变量、
   `TimeZoneController`（blink 侧 `blink::TimeZoneController`）、
   `content` 的 `SetTimeZoneOverride`（devtools 路径）——推荐 browser 侧
   覆盖点（`content::ContentBrowserClient::` 有无 timezone 钩子？
   `base::Timezone` / `icu::TimeZone` 在 browser 进程的设置时机）。
   产出进同一文档的独立章节。

## 产出

`docs/batch4-language-timezone-design.md`（中文，格式对齐前两批设计文档）：
出口核实（行号）、机制选择论证、wire 字段 → 注入映射、测试先行计划
（bootstrap_unittests 用例 + 真浏览器 navigator.language / Accept-Language /
Intl.DateTimeFormat().resolvedOptions().timeZone 验证矩阵）、明确不做清单。
每个论断带 文件:行号。提交到本分支。
