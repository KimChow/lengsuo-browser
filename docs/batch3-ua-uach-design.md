# 第三批设计：UA/UA-CH 画像覆盖

状态：**设计定稿（2026-10-16，基于本地 Windows 固定树逐行核实）**，
实现未开始；catalog 解码契约见 `docs/catalog-contract.md`（已落地）。
本批被 0001–0005 已验证的 bootstrap 状态机制直接支撑。

## 注入出口（155 固定树核实，修正 1.47 笔记的旧行号）

- `ChromeContentBrowserClient::GetUserAgent()`
  （chrome_content_browser_client.cc:7733，薄封装
  `embedder_support::GetUserAgent()`）。
- `ChromeContentBrowserClient::GetUserAgentMetadata()`（:7737，薄封装
  `embedder_support::GetUserAgentMetadata()`）。

消费方（核实于同树）：

- 网络栈：NetworkContext 的 user_agent 取自 `GetUserAgent()`
  （content_browser_client.h:2142 注释）。
- renderer：`RenderProcessHostImpl::Init`（render_process_host_impl.cc:2037）
  把 `GetUserAgent()` 与 `GetUserAgentMetadata()` 两值经
  `InitializeRenderer(...)` 直送 renderer；`WebContentsImpl::
  GetUserAgentOverride()` 返回 `renderer_preferences_.user_agent_override`，
  `NavigationRequest::GetUserAgentOverride()`（navigation_request.cc:11357）
  在导航与 UA-CH 中消费。

因此 **browser 进程各一处覆盖即可同时满足** JS UA、UA-CH、client hints
与网络栈，复用现有传播通道，不新增 mojom——与第一/二批纪律一致。

## 值映射（CatalogUaEntry → blink::UserAgentMetadata）

| catalog 字段 | UserAgentMetadata 字段 |
| --- | --- |
| brands | brand_version_list |
| full_versions | brand_full_version_list |
| platform | platform |
| platform_version | platform_version |
| architecture | architecture |
| bitness | bitness |
| model | model |
| mobile | mobile |

明确不写（非伪造纪律，保持上游默认）：`full_version`（单值，已废弃但
上游仍在填）、`wow64`、`form_factors`。其中 `form_factors` 是否从
`mobile=false` 推导 `Desktop` 属语义决定（类比第二批 available_rect=rect），
需产品确认后单独落地，不得默认推导。

`GetUserAgent()` 返回 `entry.ua_string`，无推导拼接。

## 加载与绑定契约

- 新 opt-in 参数 `--lengsuo-catalog-wire=<absolute-local-file>`，路径
  安全规则与 `--lengsuo-profile-wire` 完全一致（绝对本地路径、大小上限、
  解码失败即拒启）。
- catalog 可选：无参数时 UA/UA-CH 保持上游；有 profile 无 catalog 同样
  保持上游（第一批行为不变）。
- 绑定：加载时执行 `CatalogMatchesProfile`，pool_version 不一致按非法
  输入处理——拒绝启动，不回退不伪造。
- 条目解析：唯一使用 `slot == profile.slots[5]`（ua 槽位）的条目；
  解码器已保证条目间 slot 不重复。

## 测试先行（bootstrap_unittests）

1. 无 catalog：UA 覆盖函数返回空指针/上游行为，出口直通 embedder。
2. 有 catalog 且绑定成功：slot 命中解析，字段逐一映射。
3. pool 不一致：初始化拒绝，进程内不可替换。
4. 无匹配 slot 条目：出口保持上游（catalog 合法但槽位缺失是授权问题，
   由 runner 侧契约负责，browser 不猜）。

红→绿流程要求 bootstrap_unittests 先编译失败/断言失败再实现 0006 补丁；
本地 Windows 构建进行中，1.47 需复跑同套件。

## 明确不做

- DevTools UA 覆盖与画像的优先级仲裁（第二批已列为未验证项，继续挂起）。
- WebView/guest 路径的特殊化（guest 读自己的 RendererPrefs；本批按
  browser 进程统一画像处理，行为一致性专项验证后才能声明）。
- 值池内容：catalog 值由授权 runner 提供，仓内只用合成值测试。
