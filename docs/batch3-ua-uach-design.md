# 第三批设计：UA/UA-CH 画像覆盖

状态：**UA 字段基础接入已验证（2026-10-17）**。0006 补丁和 catalog
启动加载已进入 Linux、Windows 完整 chrome 编译，两平台原生单测8/8通过；
主页面、Dedicated Worker 与 HTTP 请求头的字段一致性已实测通过。
该结论只覆盖下列 catalog 字段，不代表完整指纹浏览器验收通过。
catalog 解码契约见 `docs/catalog-contract.md`（已落地）。
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
4. 显式提供 catalog 却无匹配 slot 条目：拒绝启动，避免把失败的显式
   画像配置当作上游成功启动。catalog 单独出现而没有 profile 也拒启。
5. 空、超长、损坏、错池 catalog 拒启；成功或失败后均不可替换状态。
6. 字段映射覆盖已有列表、清空空列表，保留 catalog 未携带的上游字段。

新增 resolver/startup API 用例先编译失败，修复后通过；Windows 旧 chrome
在真实页面用例中仍返回 HeadlessChrome/155，上游值断言失败；新版本通过。

## 复现与证据

两平台执行同一仓内测试脚本（每次指定新的仓外 work 目录）：

```text
python tests/browser_ua_smoke.py --chrome /absolute/path/to/chrome --repo /absolute/path/to/lengsuo-browser --work /absolute/path/to/new-test-directory
```

脚本只绑定127.0.0.1临时端口、使用合成画像及值池，测试目录保留以供诊断。
主页面、Worker的 UA/brands/platform/fullVersionList/architecture/bitness 等
读取值与实际 HTTP User-Agent / Sec-CH-UA-* 对照；不使用外部检测站评分。
正负路径：完整画像与 catalog、无参数、仅画像、仅 catalog、错池、缺槽位、
损坏、空文件、相对路径，共9条；有效3条rc=0，非法6条rc=5。

- Windows：`D:/lengsuo-build/ua-smoke-win/summary.json`，9条通过；
  完整浏览器增量构建 `logs/ua-win-chrome-build.log`，退出0。
- Linux：`reviews/ua-chrome-build.exit.json` rc=0，原生单测日志
  `reviews/ua-bootstrap-tests.log`；真实运行收据在 `reviews/ua-smoke-run3/`。
- catalog 196断言：Windows MSVC Release、1.47 g++及ASan/UBSan通过。

本批未覆盖 Service Worker、Shared Worker、iframe/OOPIF、DevTools覆盖与
浏览器恢复矩阵；full_version/wow64/form_factors仍保留上游，不能宣称完整
高熵画像一致性。未来catalog升级须显式补齐这些字段和相应回归用例。

## 明确不做

- DevTools UA 覆盖与画像的优先级仲裁（第二批已列为未验证项，继续挂起）。
- WebView/guest 路径的特殊化（guest 读自己的 RendererPrefs；本批按
  browser 进程统一画像处理，行为一致性专项验证后才能声明）。
- 值池内容：catalog 值由授权 runner 提供，仓内只用合成值测试。
