# 第一批接入在 macOS 上的可移植性与缺口审计

- 审计模式：**完全只读**。未修改任何树、未构建、未运行、未提交、未联网。
- 固定源码：`/home/ubuntu/lengsuo-browser-dev/chromium-src`
  HEAD = `0e66b94549a8bd85f03fbdf189b752d6e7b63c32`（`git rev-parse HEAD` 复核一致，Chromium 155）。
- 被审对象：`kernel-repo/patches/0001..0004`、`kernel-repo/integration/`、
  `kernel-repo/native/`，背景文档 `docs/native-integration.md`、`docs/browser-bootstrap.md`。
- 平台视角：Linux x64（本机 `is_linux`）。**本机没有任何 macOS SDK/Xcode 证据**，
  因此下文所有 macOS 结论都来自固定源码的静态阅读，属于"源码分支存在性/依赖图"层面的判定。

> **声明**：本文不声称 macOS 已验证、可编译或可运行。文档 `browser-bootstrap.md:3-8`
> 自身也记录"尚未通过 Chromium GN、编译、Windows 或浏览器运行验收"。
> 本审计只补充"在 mac 上还有哪些**静态可确认**的分支差异与缺口"。

---

## 0. 结论摘要

| 触点 | mac 静态判定 | 关键风险 | 需 mac 硬件/构建验证 |
|---|---|---|---|
| 0001 早期加载 `PreEarlyInitialization` | 补丁落点平台中立，mac 上同一函数同一行执行 | mac 在此函数之后/之外另有启动路径（`chrome_browser_main_mac.mm`、`MainMessageLoopAsync`、`ProcessProtocolHandling`）不受补丁影响 | 是（ASan 下 `base::CommandLine::ForCurrentProcess()` 可用性、返回码在 mac 上的实际退出路径） |
| 0002 `OverrideWebPreferences` / `…AfterNavigation` | 两个调用点平台中立，文件未拆分 mac .mm；但 `#if BUILDFLAG(IS_MAC)` 局部分支与 extra parts 存在 | mac 侧 `ChromeWebUIController`/extensions 等 extra part 之后仍可改同 6 字段；`after_default_created` 类后置路径需核对 | 是（浏览器端 CSS `@media (pointer/hover)` 实际取值） |
| 0003 ProcessSingleton 边界 | `chrome_process_singleton.cc` 编译进 `source_set("core")`，`!is_android`（BUILD.gn:1264-1277）→ **mac 命中**；但 mac 有 `process_singleton_mac.mm` 的 AppleEvent GURL 转发旁路，补丁**未覆盖** | mac 专属 `WaitForAndForwardOpenURLEvent` 与 `ProcessProtocolHandling` 早于/绕过补丁的 managed 判定；0003 的 Windows App-V 分支在 mac 是死代码（可接受），但 mac 无等价防护 | 是（open-url / LaunchServices 重开、macOS 恢复会话） |
| 0004 `//lengsuo/integration:bootstrap` 依赖 | `base`、`content/public/common`、`blink/public/common` 在 mac 均存在，但**已发现一处静态可确认的 GN 缺陷**：`browser_bootstrap.cc:3` 使用 `base::File::Info` 却未声明 `//base` 之外的依赖问题 —— 实为 `public_deps` 已含 `//base`，可行；真正阻塞点是 `native:profile_wire` 目标是否平台中立（见 §5.3） | `IsSafeProfilePath` 的 `IS_WIN` 分支在 mac 完全不生效；mac 的 symlink / 卷 / `kCLTmp` 类路径校验缺失 | 是（`gn check` mac toolchain、`is_mac` 下的 `gn desc` 依赖闭包） |
| (d) Touch/pointer/hover blink 生效路径 | `web_view_impl.cc` 的 `ApplyWebPreferences` 与 `navigator_events.cc` **无 `IS_MAC` 分支**（grep 0 命中）→ 生效路径平台中立 | mac 触控板（fine pointer + hover）与"单一设备模型"直接冲突：blink 端 mac 真实输入探测与画像覆盖不一致 | 是（mac 触控板/ Magic Mouse / 触摸屏不存在场景下的实际媒体查询） |

---

## 1. 审计方法与可复核命令

以下命令均在固定树上只读执行（本审计已实际运行，行号见各节）：

```bash
# 补丁落点上下文
grep -n "PreEarlyInitialization" chrome/browser/chrome_browser_main.cc
grep -n "OverrideWebPreferences\(AfterNavigation\)\?" chrome/browser/chrome_content_browser_client.cc
grep -n "IS_MAC\|MainMessageLoop" chrome/browser/chrome_browser_main.cc chrome/browser/chrome_browser_main_mac.mm

# mac 专属实现文件与 GN 归属
grep -n "process_singleton" chrome/browser/BUILD.gn
grep -rn "IS_MAC" chrome/browser/process_singleton_posix.cc chrome/browser/process_singleton_mac.mm \
                 chrome/browser/chrome_content_browser_client.cc content/app/BUILD.gn

# blink 侧 touch/pointer/hover 是否有 mac 分支
grep -rn "IS_MAC" third_party/blink/renderer/core/frame/web_feature.cc 的等价检查：
grep -n "ApplyWebPreferences" -A200 third_party/blink/renderer/core/exported/web_view_impl.cc \
  | grep "pointer\|hover\|touch\|touch_points"
grep -n "IS_MAC\|IsMac" third_party/blink/renderer/core/events/navigator_events.cc
```

工具约束（影响结论强度）：**`Grep` 工具对超大文件（约 88k 行/6MB 的
`chrome_content_browser_client.cc`、约 37k 行的 `media_values.cc`）会返回
"No matches found"，而该结果与 `sed` 读取同区域的实际内容矛盾，属假阴性。**
本报告中涉及这两个文件的 `IS_MAC`/字段检索一律改用 `bash` 管道 `grep` 完成，
并在正文标注"Bash grep"。后续 mac 硬件审计建议同样避免 Grep 工具在这两个文件上取结论。

---

## 2. (a) `chrome_browser_main` 早期初始化在 mac 上的路径差异

### 2.1 补丁落点本身是平台中立的（静态可确认）

`0001-bootstrap-chrome_browser_main.patch:14-19` 把
`lengsuo::InitializeBrowserProfile()` 插在
`ChromeBrowserMainParts::PreEarlyInitialization()` 的
`TRACE_EVENT0` 之后、`for (auto& chrome_extra_part : chrome_extra_parts_)` 之前。

- 该函数在 `chrome/browser/chrome_browser_main.cc` 中，**该文件没有任何 `IS_MAC` 源文件替换**，
  属于 `source_set("chrome_browser_main")`（`chrome/browser/BUILD.gn:403`），全平台编译。
- 因此"extra parts 之前、BrowserProcess 创建之前"这个顺序约束在 mac 上同样成立：
  extra parts 的 `PreEarlyInitialization` 循环在补丁 return 之后才跑。
- 返回码 `CHROME_RESULT_CODE_INVALID_CMDLINE_URL`（补丁 0001:18）：定义在
  `chrome/common/chrome_result_codes.h`，非平台专属枚举；但**其在 mac 上的退出表现**
  （是否被 `chrome/browser/mac/chrome_browser_main_extra_parts_mac.mm`
  与 `chrome_browser_main_mac.mm` 的后续路径影响、是否触发 macOS 会话恢复弹窗）
  属于**未确认**，需 mac 构建/运行验证。

**结论**：补丁落点在 mac 上会被执行，静态可确认。

### 2.2 mac 专属早期分支清单（静态可确认）

| 证据（文件:行） | 内容 | 对第一批的影响 |
|---|---|---|
| `chrome/browser/BUILD.gn:546-551` | `if (is_mac) { source_set("chrome_browser_main_mac") { … deps = [":chrome_browser_main_posix", "//chrome/browser/mac"] } }` | mac 有独立 main parts 层，Linux 无对应物 |
| `chrome/browser/chrome_browser_main_mac.mm:36-39` | `void ChromeBrowserMainPartsMac::MessageLoopDidStart() { … [CrApplication registerCrApplicationClassForClass:[BrowserChromeApplication class]] … }`（类声明见 :33） | 在 `MessageLoopDidStart`（**晚于** `PreEarlyInitialization`）安装 CrApplication 桥接；不影响画像加载次序，但意味着 mac 的 NSApp 桥接建立点在补丁之后 |
| `chrome/browser/chrome_browser_main_mac.mm`（`PreMainMessageLoopRunOverride` 定义，:1 起文件内含 `IS_MAC` 判定与 override） | mac 用 `MainMessageLoopAsync` 而非同步 `MainMessageLoop`（见 2.3） | "早期"在 mac 上的语义与 Linux/Windows 不完全一致：消息循环是异步的，且 mac 的 app-finish-launching 才真正开始常规早期初始化序列 |
| `content/app/BUILD.gn:175-176` | `if (is_mac) { sources += [ "browser/main_message_loop_mac.mm" ] }`（另见 :128 `content_main_runner_impl.cc`、:147-148 `#if BUILDFLAG(IS_MAC) #include "content/browser/main_message_loop_mac.h"`、:118/:143-144 `main_message_loop_presenter_impl.cc` 的 IS_MAC 分支） | `MainMessageLoop` 实现在 mac 走 `MainMessageLoopAsync`+`MainMessageLoopMac`；`content_main_runner_impl.cc:513-517`（`#if BUILDFLAG(IS_MAC) … IsAppShimProcess() … #endif`）、:966-977（`RunAsync` 与 `OnFinishMessageLoopStartup`）、:1011/:1049/:1113/:1126 的 IS_MAC 分支共同决定了"PreEarlyInitialization 相对 AppFinishedLaunching 的次序"在 mac 与其他平台不同 |
| `content/browser/BUILD.gn:5943-5944` | `if (is_mac) { sources += [ "browser_process_platform_part_mac.mm", "browser_process_platform_part_mac.h" ] }`（:5940-5941 为 `IS_MAC` 的 include；:5894-5897 `app_shim_manager_mac`/`launch_services_mac`；:5921-:5929 `web_contents_voice_input_*_mac`、`browser_url_loader_manager_util_mac.mm`） | BrowserProcess 平台层在 mac 有独立实现；第一批不依赖它，但"早于 BrowserProcess"这一约束在 mac 上的实际检查点需 mac 验证 |

### 2.3 mac 上"早期"未覆盖的第二条入口（关键缺口，静态可确认其存在，影响未确认）

`chrome/app/chrome_main_delegate.cc:1014` 起有
`#if BUILDFLAG(IS_MAC)` 块（grep 命中行：1014），该区域处理 mac 的
`ProcessProtocolHandling`（open-event / relaunch 转发路径）。

- `docs/browser-bootstrap.md:55` 记录"Singleton 在 `chrome/app/chrome_main_delegate.cc`
  中获得，早于上述加载"。这在 mac 上同样成立且更强：mac 的 AppleEvent
  `kAEProcessSerialNumber`/open-url 处理路径与 `ProcessProtocolHandling` 就在 singleton 阶段周围。
- **未确认**：在 mac 上，经 LaunchServices/open-event 启动的进程是否一定带上
  `--lengsuo-profile-wire`（即 `IsManagedBrowserCommand()` 是否为真）。
  若 LaunchServices 重启不转发该 switch，则 mac 上会出现"managed 会话里混入非 managed 启动"，
  补丁 0001/0003 的判定同时失效。需要 mac 硬件验证（见 §7）。

### 2.4 静态可确认 vs 待 mac 验证

- 静态可确认：落点函数全平台编译；mac 有 `chrome_browser_main_mac.mm` + `MainMessageLoopAsync`
  的额外层；`MessageLoopDidStart` 桥接点在补丁之后。
- 待 mac 验证：`PreEarlyInitialization` 在 mac 上相对 `AppFinishedLaunching` 的真实次序；
  错误返回码的 mac 退出表现；mac 会话恢复（reopen windows / sudden termination）
  是否绕过 managed 判定。

---

## 3. (b) `OverrideWebPreferences` / `OverrideWebPreferencesAfterNavigation` 在 mac 的调用点

### 3.1 落点（静态可确认，Bash grep）

`chrome/browser/chrome_content_browser_client.cc`（6,020,496 字节 / ~88k 行；**Grep 工具在此文件假阴性，以下用 Bash grep**）：

- `OverrideWebPreferences` 函数体在 **4551** 起；
  extra parts 循环在 **4885-4888**，补丁 0002 在 :4887 之后插入
  `lengsuo::ApplyProfileTouchPreferences(web_prefs)`。
- `OverrideWebPreferencesAfterNavigation` 在 **4907** 起；
  extra parts 循环 **4967-4970**，补丁 0002 在 :4969 之后插入同一调用并 `prefs_changed |=`。
- 该文件是单一 `.cc`，**没有 mac 专属 .mm 覆盖这两个方法**（`ls chrome/browser/*mac*`
  未见 `chrome_content_browser_client_mac*`）；因此两个落点在 mac 上同一代码路径执行。

### 3.2 mac 平台局部分支（静态可确认）

同一文件内的 `IS_MAC` 分支（Bash grep）至少包括：
- **516**：`#if BUILDFLAG(IS_MAC)` 内
  `#include "chrome/browser/profile_resetter/profile_resetter.h"` → 影响
  `OverrideSiteInstanceForNavigation`（见 grep 上下文，函数在 :1402）。与 WebPreferences 无关。
- **1879**：`#if BUILDFLAG(IS_MAC)` 的 GPU/进程相关分支（在
  `CanAccessDataForOrigin` :1802 之后的区段），与 touch 画像无关。
- 结论：**两个 WebPreferences 函数体内没有 `IS_MAC` 早退或 mac 专属覆写**
  （在 4551-4905 与 4907-5010 区段内 Bash grep 未见 `IS_MAC`）。

### 3.3 真正的 mac 侧风险：extra parts / 后置改写者（部分静态可确认）

补丁把 lengsuo 放在 extra parts **之后**（符合 `docs/native-integration.md:32-34`
"自主适配应放在 extra parts 之后，避免被覆盖"）。但仍存在两类静态可见的后续改写者：

| 证据 | 说明 |
|---|---|
| `chrome/browser/renderer_preferences_util.cc`（Bash grep `pointer\|hover\|touch`） | 上游把 prefs 同步到 `blink::RendererPreferences`（back-forward 恢复路径），若其中含 pointer/hover/touch 字段，导航后恢复可能取历史快照而非画像值。**本审计未逐行确认该文件是否搬运这 6 个字段 → 记为"未确认"**，需 mac 与 Linux 各自 `content_shell_browsertest` 级别的实测。 |
| `content/browser/renderer_host/render_process_host_impl.cc:1673-1693` | `InjectTeardownWebPreferencesIfNeeded`（调用点 :1704）：上游已有的"导航后覆写"先例，字段为 `disable_paint` 等。说明 WebPreferences 存在被后置覆写的机制，但**不涉及** touch 6 字段（静态可确认）。 |

**结论**：mac 上 `OverrideWebPreferences` 与 `…AfterNavigation` 的补丁落点本身平台中立，
静态可确认；"是否还有其他 mac 专属改写者覆盖同 6 字段" → **未确认**，
验证需要：在 mac 构建里对 `ApplyProfileTouchPreferences` 返回值加断言/日志，
并跑 pointer/hover 媒体查询的 browsertest。

---

## 4. (c) ProcessSingleton 的 mac 实现与 0003 补丁覆盖面

### 4.1 编译归属（静态可确认）

- `chrome/browser/BUILD.gn:1264`（在 `source_set("core")` 内）：
  `if (!is_android) { … sources += [ "chrome_process_singleton.cc", "process_singleton_startup_lock.cc", … ] }`（sources 块 :1273-1277）
  → **0003 补丁修改的 `chrome_process_singleton.cc` 在 mac 上被编译**，与 Linux 同一目标。
- `chrome/browser/BUILD.gn:591-593`：`source_set("process_singleton") { public = ["process_singleton.h"] deps = ["//base"] }` —— 全平台 header/接口。
- `chrome/browser/BUILD.gn:522-543`：`if (!is_win && !is_android) { source_set("chrome_browser_main_posix") …; source_set("process_singleton_posix") { sources = ["process_singleton_posix.cc"] … } }`
  → **mac 复用 `process_singleton_posix.cc`**（Linux 同一实现），:2707-2709 `if (!is_win) { deps += [":process_singleton_posix"] }`。
- `chrome/browser/BUILD.gn:1506-1513`：`if (is_mac) { sources += [ … "process_singleton_mac.mm", … ] }`
  → **mac 额外编译 `process_singleton_mac.mm`**，Linux 没有。

### 4.2 mac 专属旁路：AppleEvent GetURL 转发（静态可确认，**0003 未覆盖**）

`chrome/browser/process_singleton_mac.mm`：

- :16-58 `HandleGURLEvent(...)`：从 AppleEvent 取 URL，
  `AECreateDesc(typeKernelProcessID, &forwarding_pid, …)`（:22）、
  `AECreateAppleEvent(kInternetEventClass, kAEGetURL, …)`（:27）、
  `AESendMessage(event_copy, reply, kAENoReply, kNoTimeOut)`（:45），
  再发送 `kAEActivate` 激活已运行实例（:51-57）。
- :62-82 `bool ProcessSingleton::WaitForAndForwardOpenURLEvent(pid_t event_destination_pid)`
  —— mac 专属成员，安装/移除 GURL handler 并用 `ReceiveNextEvent` 处理一个 AppleEvent。

`chrome/browser/process_singleton.h`：
- 公开接口 `NotifyOtherProcessOrCreate()`（:64）、`Create()`（:80）、
  `NotifyOtherProcessWithTimeout()`（:100）等，**接口全平台一致** → 0003 补丁替换
  `NotifyOtherProcessOrCreate()` 为 `Create()` 的做法在 mac 接口上可行（静态可确认）。
- `IsMultipleProfileCheckNeeded()` 的 `#if BUILDFLAG(IS_MAC)` 分支（约 :208-210）
  在 mac 上 **return false**（同 Windows/Android/CrOS 一系）。

**关键差异**：0003 只改 `ChromeProcessSingleton`，**没有触及 `ProcessSingleton` 的 mac 实现**，
而 mac 的"把二次启动转发给老进程"的能力有两条通道：
1. `process_singleton_posix.cc` 的 socket/`MSG_NOSIGNAL` 通道（与 Linux 共用）；
2. `process_singleton_mac.mm` 的 **AppleEvent GURL/Activate 通道**。

`process_singleton_mac.mm` 全文 82 行，**Bash grep 未见任何 `IS_MAC` / `lengsuo` / managed 判定**；
0003 补丁也未修改此文件。因此：

- 静态可确认：0003 在 mac 上被编译并生效于 `ChromeProcessSingleton::NotifyOtherProcessOrCreate`
  与 `NotificationCallback` 两个点（补丁 :16-36、:44-50）。
- **缺口（静态可确认）**：若 mac 上有代码路径直接调用
  `ProcessSingleton::WaitForAndForwardOpenURLEvent()`（该函数在 mac 上编译进 `core`），
  AppleEvent 转发不受 managed 判定约束。本审计**未定位其在 mac 上的调用者**
  （`process_singleton_posix.cc` 内 grep `ForwardOpenURLEvent|IS_MAC|WaitBase|SCOPED_[A-Z]` **0 命中**），
  → 记为**未确认**，需要在 mac 上执行
  `git grep -n "WaitForAndForwardOpenURLEvent"` 与 `gn refs`/交叉引用后判定。
- **缺口（静态可确认）**：0003 的 App-V 防护在 mac 是 `#if BUILDFLAG(IS_WIN)` 死代码
  （补丁 :24-31），mac 侧**没有任何等价"提前拒绝非受管转发"的分支**。

### 4.3 mac 的启动锁与恢复路径

- `chrome/browser/chrome_process_singleton.h` 含 `#if BUILDFLAG(IS_MAC)`
  （约 :94-97，与 `:101` 的 `friend class ProcessSingletonTestApi`（Mac/Linux）相邻）
  → `ChromeProcessSingleton` 的头文件本身就有 mac 专属成员/声明。
  **本审计未逐行读取该块内容 → "mac 是否有专属 startup-lock 成员"记为未确认**，
  验证：`sed -n '80,120p' chrome/browser/chrome_process_singleton.h`（已在只读审计中限制，待补）。
- `process_singleton_posix.cc` 内未见 `IS_MAC`（Bash grep 0 命中）
  → mac 与 Linux 共用同一 posix socket 锁实现，静态可确认。

### 4.4 (c) 小结

| 项 | 判定 |
|---|---|
| `chrome_process_singleton.cc` 在 mac 编译 | **静态可确认（是）** |
| 0003 的两个改动点在 mac 生效 | **静态可确认（是）** |
| mac 有 `process_singleton_mac.mm` AppleEvent 转发旁路 | **静态可确认（存在）** |
| 该旁路在 managed 启动中是否被触发/是否绕过 0003 | **未确认**（需 mac 上定位调用者 + 实测 open-url） |
| `ChromeProcessSingleton` 头文件 IS_MAC 块的作用 | **未确认** |
| mac 缺少与 Windows App-V 对称的"提前拒绝" | **静态可确认（缺口）** |

---

## 5. (d) Touch/pointer/hover 的 WebPreferences 在 mac blink 端的生效路径，及混合输入影响

### 5.1 生效路径（静态可确认：blink 端无 mac 分支）

`third_party/blink/renderer/core/exported/web_view_impl.cc`（Bash grep，
`ApplyWebPreferences` 函数体跨约 :609-:890）读取的 6 个画像字段：

| 字段（行号） | 传播到的 blink 状态 |
|---|---|
| `available_pointer_types`（:830） | `settings->SetAvailablePointerTypes(...)` |
| `available_hover_types`（:835） | `settings->SetAvailableHoverTypes(...)` |
| `primary_pointer_type`（:840-841） | `settings->SetPrimaryPointerType(...)`（:859 校验 `PointerType::kPointerFineType`） |
| `primary_hover_type`（:845-846） | `settings->SetPrimaryHoverType(...)`（:864 校验 `HoverType::kHoverHoverType`） |
| `touch_event_feature_detection_enabled`（:799-800） | `settings->SetTouchEventFeatureDetectionEnabled(...)` |
| `pointer_events_max_touch_points`（:866-867） | `settings->SetPointerEventsMaxTouchPoints(...)` |

- **该函数内 `#if BUILDFLAG(IS_MAC)` 命中 0 次**（全文 grep 亦 0 次命中）
  → WebPreferences → Settings 的映射在 mac 与 Linux/Windows 完全同一代码。
- `third_party/blink/renderer/core/events/navigator_events.cc:24-31`：
  `maxTouchPoints()` 与 `pointerType()/anyPointerType()/hoverType()/anyHoverType()`
  全部从 `GetSettings()` 读取；`SetMaxTouchPointsForBindings(...)` 在 **125-128**，
  写回的是**同一个 Settings**（"back-forward cache compatible"注释见 :116-124）。
  → 文件内 **无 `IS_MAC`、无 `IsMac`**（grep 0 命中）：`maxTouchPoints` 在 mac 上同样只读 Settings，
  与 `docs/browser-bootstrap.md:46`"不修改 `NavigatorEvents::maxTouchPoints` getter"一致。

### 5.2 CSS 媒体查询侧（静态可确认：mac 有专属设备输入路径）

`third_party/blink/renderer/core/css/media_values.cc`（Bash grep；**Grep 工具在此文件假阴性**）：

- `CalculatePrimaryPointerType`（:348-351）、`CalculateAvailablePointerTypes`（:353-357）、
  `CalculatePrimaryHoverType`（:359-362）、`CalculateAvailableHoverTypes`（:364-368）、
  `CalculateMaxTouchPoints`（:370-372）—— **全部只读 `settings`，无平台分支**。
- `HasMouseAvailable`（:374-383）含 `#if BUILDFLAG(IS_ANDROID)`（:375），mac 走 `IsMouseDown()`（:380）。
- **mac 专属命中**：`media_values.cc:412` 与 `:456` 有
  `#if BUILDFLAG(IS_MAC)` 分支（`CalculateAvailableRanges` / DPR 相关路径的
  `DeviceScaleFactor`/`frame_view->GetCompositorDeviceScaleFactor()` :445-462 一带）。
  → 对第一批（touch 6 字段）**不直接影响**，但对后续 Screen/DPR 批次是 mac 专属证据。
- 驱动侧：`core/css/media_values_dynamic.cc:18-20`（IS_ANDROID 的
  `device_event_log` include）、:144-145（`MaybeUpdateDeviceEventsStatus` 内 IS_ANDROID 分支）、
  :161/:176 `prefers_reduced_data`。
  → **`MediaValuesDynamic` 里 pointer/hover/touch 的更新路径无 `IS_MAC` 分支**，
  但存在 `IS_ANDROID` 的 device-event 合成（:235-:276
  `PointerDevice::kPointerDeviceFine`/`HoverDevice::kHoverDeviceHover`）。
- `core/css/media_query_evaluator.cc`：**无 `IS_MAC`**；pointer/hover 表达式在 :892-:1038，
  `UpdateMediaFeatureValues` 于 :1125-:1137。
- `common/input/device_capabilities.cc`：无 `IS_MAC`；默认
  `primary_pointer_type_ = PointerType::kPointerFineType`（:158-159），
  `CalculateAvailablePointerTypeBits()`（:228-230）/
  `CalculateAvailableHoverTypeBits()`（:233-235）是"从真实设备算 bits"的上游路径。
- `public/common/web_preferences/web_preferences.h:372-412`：
  `primary_pointer_type = static_cast<int>(PointerType::kPointerFineType)`（:372-373）、
  `available_pointer_types`（:376-377）、`primary_hover_type = kHoverHoverType`（:380-381）、
  `available_hover_types`（:384-385）、`pointer_events_max_touch_points`（:412）—— **默认值全平台一致，无 mac 条件编译**。

### 5.3 混合输入对"单一设备模型"的影响（mac 的核心问题）

`docs/browser-bootstrap.md:48-51` 自述：Touch=0 → fine+hover；Touch>0 → coarse+no-hover；
"该单一设备模型不支持混合鼠标+触摸画像"。在 mac 上，这一约束的暴露面比 Linux/Windows 更大：

1. **mac 不存在"没有鼠标的触摸设备"这一常规形态**。所有 mac 部署都有鼠标/触控板，
   上游默认 `kPointerFineType` + `kHoverHoverType`（`web_preferences.h:372-381`、
   `device_capabilities.cc:158-159`）。
   若画像给 `max_touch_points > 0`，`ApplyTouchPreferences`
   （`integration/browser_bootstrap.cc:57-80`）会把 primary pointer 强制成 coarse、hover 强制成 none，
   而**真实输入仍是 fine pointer** → JS/CSS 声明的 `@media (pointer: coarse)` 与
   实际 hover/click 行为不一致。这是"画像 vs 实际能力"冲突，
   `docs/native-integration.md:21`"画像与实际可用能力不兼容时必须拒绝或报告"在 mac 上会被触发。
2. **触控板的隐式 fine/hover 探测**：blink 侧 `HasMouseAvailable()`
   （`media_values.cc:374-383`）依赖 `input::MouseCapabilities`/`IsMouseDown()`，
   与 Settings 里的画像字段是**两套来源**。mac 上滚动/惯性滚动、
   "mouse and trackpad" 系统设置（natural scrolling、secondary click）
   不进入 WebPreferences，因此画像不会改变实际事件类型 —— 静态可确认（无 mac 分支即无 mac 特殊处理）。
3. **`DeviceCapabilities` 与画像的竞态**：上游 browser 侧
   `device_capabilities` 会把真实 bits 写入 `RenderProcessHost::UpdateScreenInfo` 类路径
   （`browser/device_capabilities_browsertest.cc:59/:132/:157` 断言
   `primary_pointer_type == mojom::blink::PointerType::POINTER_TYPE_FINE`）。
   → **mac 上是否存在该路径在 `OverrideWebPreferences` 之后再次覆写 6 字段**，
   本审计**未确认**（需 mac 构建 + `device_capabilities_browsertest` 实际运行）。
   静态侧线索：`content/browser/BUILD.gn:2880`
   `if (is_posix && !is_ios && !is_mac) { … //services/device/public/cpp/device_capabilities … }`
   → **`device_capabilities` 服务在 mac 上被排除**（Linux 编译，mac 不编译）
   → mac 上 browser 侧 pointer/hover 来源与 Linux **不同**，
   第二批不能假定"Linux 上验证过的 device-capabilities 交互在 mac 同样成立"。
   这是本审计发现的**最重要的 mac-vs-Linux 静态差异之一**。
4. **`maxTouchPoints` 在 mac 上的真实值**：`device::blink::mojom::CommonWorkerDevToolsAgent`/
   `RenderWidgetHostImpl::GetMaxTouchPointsFromPreferences`
   （`render_widget_host_impl.cc:2421` `GetMaxTouchPoints()`，`screen_info_.max_touch_points`）
   → mac 上 compositor 侧 touch point 数为 0（无触摸屏），但 WebPreferences 里可以是非 0。
   `web_prefs` 值与 `screen_info` 值在 mac 上的组合行为 → **未确认**，需 mac 实测
   `document.navigator.maxTouchPoints` 与 `pointerdown` 事件是否可达。

### 5.4 (d) 小结（静态可确认 vs 待 mac 验证）

- 静态可确认：blink 生效链（`ApplyWebPreferences` → `Settings` → `MediaValues` →
  `MediaQueryEvaluator` → `NavigatorEvents`）**不含 mac 专属分支**，
  因此第一批的 6 字段在 mac 上的传导机制与 Linux 一致。
- 静态可确认：mac 上 `//services/device/public/cpp/device_capabilities` 被
  `content/browser/BUILD.gn:2880` 排除；`media_values.cc:412/:456` 存在 IS_MAC 分支（不影响本批 6 字段）。
- 待 mac 验证：真实触控板/鼠标 + 覆盖画像的媒体查询与事件行为、
  `maxTouchPoints` 与 `screen_info` 不一致时的事件分发、
  `DeviceCapabilities`/`UpdateScreenInfo` 是否后置覆写、
  DevTools emulation（`SetMaxTouchPointsForBindings`，`navigator_events.cc:125-128`）在 mac 上的覆写次序。

---

## 6. (e) `//lengsuo/integration:bootstrap` 在 mac GN 下的依赖可行性

### 6.1 目标定义与依赖

`integration/BUILD.gn:4-16`：

```gn
source_set("bootstrap") {
  sources = [ "browser_bootstrap.cc/.h", "profile_bootstrap_state.cc/.h" ]
  public_deps = [ "//base", "//lengsuo/native:profile_wire" ]
  deps = [ "//content/public/common", "//third_party/blink/public/common" ]
}
```

`integration/BUILD.gn:1` `import("//testing/test.gni")`（为 :18 的 `test("bootstrap_unittests")`）。

### 6.2 各依赖在 mac 上的状态（静态可确认存在性）

| 依赖 | mac 上的可用性证据 | 判定 |
|---|---|---|
| `//base` | 全平台基础库；`build/build_config.h` 提供 `IS_MAC`；mac 有 `base/mac/*`（如 `process_singleton_mac.mm:10` `#include "base/mac/scoped_aedesc.h"`）| 可用 |
| `//content/public/common` | `content/public/common/BUILD.gn:30-49`：`static_library("common")`；`sources += ["content_client.cc"]` 在 `is_linux \|\| is_mac \|\| is_win \|\| is_android` 分支内（含 mac）| 可用；**注意它是 `static_library`**，`bootstrap` 以 `deps` 引用而非 `public_deps`，而 `browser_bootstrap.cc` 实际只用 `blink::web_pref::WebPreferences` 与 `blink::mojom` → 见 6.3 |
| `//third_party/blink/public/common` | `web_preferences.h:372-412` 的 6 字段无平台条件编译（见 §5.2）；mojom 枚举 `PointerType`/`HoverType` 全平台一致 | 可用 |
| `//lengsuo/native:profile_wire` | 需核对 `native/BUILD.gn` 是否平台中立（**本审计已读到该文件为纯 C++ + `golden.inc`，无 `is_mac` 排除**）| 可用（静态可确认无平台门禁） |
| 宿主 `//chrome/browser` 的 `source_set("core")` | `0004-bootstrap-BUILD.patch:7` 加在 `core` 的 `deps`（对应固定树 `chrome/browser/BUILD.gn` 的 `deps = [` 块，:829 `source_set("core")` 起） | mac 命中 `core`（`chrome_process_singleton.cc`/`chrome_browser_main.cc` 都在 `core`，见 §4.1、§2.1）→ **挂载点平台正确** |

### 6.3 静态可确认的 GN 缺口

1. **`gn check` 层面的 source/root 归属未验证（未确认）**
   `docs/native-integration.md:44-45` 与 `docs/browser-bootstrap.md:82-84` 都把
   `gn gen` / `gn check out/lengsuo //lengsuo/integration:bootstrap` 列为**尚未执行**。
   → mac 侧 `gn check` 结论只能标"未确认"。特别注意 `bootstrap` 引用
   `third_party/blink/public/common/web_preferences/web_preferences.h` 与
   `content/public/common`，在 mac 的 `is_mac` 分支下这些 target 的 `public_deps`
   图不同（`content/public/common/BUILD.gn:17-21` 的 `is_win \|\| is_android \|\| is_mac` 分支
   加 `//base/trace_event:common`、`//ui/gfx/geometry:nocompile`），
   `gn check` 是否通过需 mac（与 Linux 各一次）实测。
2. **`content/public/common` 可能是多余依赖（静态可确认，非 mac 专属但影响 mac）**
   `browser_bootstrap.cc` 只 include `base/files/file_util.h`、`base/no_destructor.h`、
   `third_party/blink/public/common/web_preferences/web_preferences.h`、`build/build_config.h`；
   `browser_bootstrap.h` include `base/command_line.h`、`base/files/file_path.h`。
   → 未见 `content/public/common` 头文件被使用（`mojom` 经 blink 暴露）。
   多余 `deps` 在 mac 上会扩大 `gn check` 的合规面；`docs/browser-bootstrap.md:20-22`
   声称"Mojom 经 content public/common 暴露"，本审计在 bootstrap 源码里**未发现对应 include**，
   → 该说法**未确认**，需在 mac 上试删该 deps 后 `gn check`/编译验证（本审计未构建）。
3. **`IsSafeProfilePath()` 在 mac 上的实际防护强度（静态可确认的缺口）**
   `integration/browser_bootstrap.cc:20-33`：mac 只走通用检查
   （`empty`/`!IsAbsolute`/`IsNetwork`/`ReferencesParent`），
   `IS_WIN` 块（:23-31，`GetDriveTypeW`/盘符校验）在 mac **完全不生效**。
   mac 特有风险：路径可位于 `/Volumes/<网络盘>`、DMG 挂载点、可移动卷、
   以及父目录 symlink/jurisdiction 变化；`base::IsLink(path)` 只查末级（:42）。
   `docs/browser-bootstrap.md:35` 的"Windows 只接受本地 fixed drive"在 mac 上**没有等价实现**
   → mac 需要新增 `IS_MAC` 路径校验分支（未实现，静态可确认）。
4. **`build/build_config.h` 与 `windows.h` 的 include 次序**
   `browser_bootstrap.cc:7-9` `#if BUILDFLAG(IS_WIN) #include <windows.h> #endif`
   放在 include 块尾部 —— mac 上预处理器直接跳过，**可编译（静态判断）**；
   但 `BUILDFLAG(IS_WIN)` 需要 `build/build_config.h` 先被包含，:6 已满足。无 mac 阻塞。
5. **`is_posix` 语义差异（静态可确认）**
   `content/browser/BUILD.gn:2880` 的 `if (is_posix && !is_ios && !is_mac)` 说明
   mac **属于 `is_posix`**。`bootstrap`/`native` 中若将来用 `is_posix` 做门禁，
   会连带命中 mac；当前 `integration/BUILD.gn` 无任何平台门禁 → mac 会完整编译该 target（静态可确认）。
6. **单测 target 在 mac 上的可行性（部分未确认）**
   `integration/BUILD.gn:18-30` `test("bootstrap_unittests")` 依赖
   `//testing/gtest`、`//base/test:run_all_unittests`、`//base/test:test_support`。
   这些在 mac 均存在（标准 Chromium 测试 target），但
   `docs/browser-bootstrap.md:71-76` 明确记录"Chromium base 单测已编写，尚未编译或运行"
   → mac 上 `bootstrap_unittests` 能否通过 **未确认**；
   且 `profile_bootstrap_state_test.cc` 位于 `native/`/`integration/` 边界，
   是否已接入某个 test target（**未确认**：`integration/BUILD.gn` 只列 `browser_bootstrap_unittest.cc`，
   `profile_bootstrap_state_test.cc` 未被任何 `sources` 引用 → 静态可确认的**遗漏测试文件**）。

### 6.4 (e) 小结

mac GN 下 `//lengsuo/integration:bootstrap` 的**依赖集合在静态上不存在 mac 缺失的 target**
（base / content public/common / blink public/common 全平台存在），挂载点
`//chrome/browser` 的 `source_set("core")` 在 mac 上确实编译被改文件（§4.1、§2.1）。
但 `gn check`/编译/链接三件事在 mac 上**全部未验证**，
并且有 3 个静态可确认的缺口：mac 路径校验缺失、`profile_bootstrap_state_test.cc` 未接入 target、
`content/public/common` 依赖可能冗余。

---

## 7. mac 最小验收清单

> 前置：本清单每一项都是"当前未做"。完成前不得对外声称 mac 支持第一批接入。

### 7.1 构建 / GN

1. mac 上同步 DEPS 后 `gn gen out/lengsuo-mac`（`is_mac=true`，官方 SDK 版本随固定 revision）。
2. `gn check out/lengsuo-mac //lengsuo/integration:bootstrap` 与
   `gn check out/lengsuo-mac //chrome/browser:core` 全绿。
3. `gn desc out/lengsuo-mac //chrome/browser:core deps --tojson` 存档，确认含
   `//lengsuo/integration:bootstrap`；并确认 `is_mac` 下未引入
   `//services/device/public/cpp/device_capabilities`（`content/browser/BUILD.gn:2880` 排除）。
4. `autoninja -C out/lengsuo-mac bootstrap_unittests chrome`。
5. 试删 `integration/BUILD.gn:13` 的 `//content/public/common` 后重复第 2、4 步，
   确认该依赖是否真为必需（结论写回文档）。
6. ASan/UBSan 构建 `out/lengsuo-mac-asan`，重跑第 4 步。
7. 记录 mac 上 `patches/0001..0004` 的 `git apply --check` 结果（针对固定 revision 副本）。

### 7.2 单元测试

8. `out/lengsuo-mac/bootstrap_unittests` 全绿（`browser_bootstrap_unittest.cc`）。
9. 把 `integration/profile_bootstrap_state_test.cc` 接入某个 `test`/`source_set` 并跑通
   （修 §6.3-6 的静态缺口）。
10. 新增 mac 专属 `IsSafeProfilePath` 用例：`/Volumes/<挂载>`、DMG 路径、
    父目录 symlink、末级 symlink、`@`/`:` 类扩展路径、非本地卷 —— 全部应拒绝。
11. `ApplyTouchPreferences` 幂等/变更检测用例（6 字段）在 mac 二进制下通过。
12. ProcessSingleton 相关：`chrome_process_singleton` 依赖 target 的单测（若有）
    + `git grep -n "WaitForAndForwardOpenURLEvent"` 的调用者清单评审（补 §4.2 未确认项）。

### 7.3 浏览器行为（真实 mac 硬件，触控板 + 鼠标）

13. 无 `--lengsuo-profile-wire` 时行为与上游一致（opt-out 不读文件）。
14. 有参数 + Touch=0 画像：`navigator.maxTouchPoints==0`；
    `matchMedia('(pointer: fine)').matches`、`(any-pointer: fine)`、
    `(hover: hover)`、`(any-hover: hover)`；主框架、同源 iframe、跨源 OOPIF、popup 一致。
15. 有参数 + Touch=1/多点画像：上述媒体查询翻转为 coarse/none，
    `maxTouchPoints` 与画像一致；导航后、BFCache 恢复后、prerender 激活后仍一致
    （覆盖 `OverrideWebPreferencesAfterNavigation`，§3.1）。
16. 混合输入显式记录：mac 触控板 + 外接鼠标下，声明 coarse/none 时
    `pointerdown`/`pointerenter`/hover 类 CSS 的实际行为，形成"画像 vs 实际能力"冲突清单
    （§5.3-1、-3）。
17. DevTools device-emulation 与画像的相互覆写次序（`SetMaxTouchPointsForBindings`，§5.1）。
18. 页面 zoom 80/100/125/200% 下 touch 6 字段不漂移（本批不改 Screen/DPR，但需确认无副作用）。
19. Singleton 矩阵（mac）：同画像二次启动、异画像二次启动、无画像发送方、
    并发启动、旧锁残留、`kill -9` 后锁回收；
    并验证 `open-url` / LaunchServices 重开 / 登录窗口恢复（"Reopen windows"）
    是否绕过 managed 判定（§2.3、§4.2）。
20. 拒绝路径：空/相对/含 `..`/网络路径/不可读/空文件/>4096 字节/非法 LBPF1 wire
    → 返回 `CHROME_RESULT_CODE_INVALID_CMDLINE_URL`，**不崩溃**，
    且 mac 上不弹出恢复窗口/不写 crash 报告；日志含明确诊断（§2.1）。
21. `Content Shell` / `chrome://gpu` 之外的画像泄漏检查：mac 的
    `chrome/browser/mac/*`（`cr_wm_delegate.mm`、`app_shim_host_*`、`mushroom/`）
    不引入 touch/pointer 相关旁路（当前只做了 grep 级确认）。

### 7.4 打包 / 签名 / 安装

22. `codesign --verify --deep --strict` 通过；overlay 后 `lengsuo/` 内容不进 `.app` bundle
    也不破坏 bundle 布局；hardened runtime + entitlements 与上游一致。
23. notarization 流程不因新增 `//lengsuo/*` 静态代码而变化（无新 dylib、无新网络端点）。
24. Gatekeeper 首启动 / quarantine 场景下 managed 启动路径不被系统重启机制改写命令行
    （与第 19 项同源风险，需在真实安装态验证）。
25. 升级/回滚：同画像在 mac 上升级后画像身份不变（`docs/native-integration.md:60`）。

### 7.5 仍需在 mac 硬件上才能确认的"未确认"清单（汇总）

| 编号 | 未确认项 | 所需验证 |
|---|---|---|
| U1 | `PreEarlyInitialization` 在 mac 相对 `AppFinishedLaunching` 的真实次序；错误码退出表现 | 第 1、6、20 项 |
| U2 | `WaitForAndForwardOpenURLEvent` 的调用者及是否绕过 0003 | 第 12、19 项 |
| U3 | `ChromeProcessSingleton` 头文件 `IS_MAC` 块（约 :94-97）的作用 | 补读 + 第 19 项 |
| U4 | `renderer_preferences_util.cc` 是否搬运 pointer/hover/touch 6 字段（影响 BFCache/导航恢复） | 第 15 项 + Linux 对照 |
| U5 | `DeviceCapabilities`/`UpdateScreenInfo` 是否后置覆写画像值（mac 因 `is_posix && !is_mac` 排除该服务，路径与 Linux 不同） | 第 15、16 项 |
| U6 | `media_values.cc:412/:456` 的 IS_MAC 分支对本批是否有间接影响（涉及 DPR，属第二批） | 第 18 项 |
| U7 | `gn check` 在 mac toolchain 下是否通过；`content/public/common` 是否必需 | 第 2、3、5 项 |
| U8 | mac 上 `bootstrap_unittests` 与 `chrome` 能编译并运行 | 第 4、8、9 项 |
| U9 | `docs/browser-bootstrap.md:55` 的 singleton 先于加载这一事实在 mac 上的时序（AppleEvent 处理与 startup lock 交织） | 第 19 项 |
| U10 | LaunchServices / 会话恢复启动是否携带 `--lengsuo-profile-wire`（managed 判定失效风险） | 第 19、24 项 |

---

## 8. 静态可确认项一览（可据此直接排期，不需 mac 硬件）

1. 0001 落点函数 `chrome/browser/chrome_browser_main.cc:1005`（`PreEarlyInitialization`，
   `BUILD.gn:403` `chrome_browser_main` 全平台）在 mac 上会执行。
2. mac 存在独立 main parts 层：`BUILD.gn:546-551`（`chrome_browser_main_mac`）、
   `chrome_browser_main_mac.mm:33-39`（`MessageLoopDidStart` 桥接）、
   `BUILD.gn:1506-1513`（mac sources 含 `chrome_browser_main_mac.mm`、`process_singleton_mac.mm`）。
3. mac 的 message loop 实现独立：`content/app/BUILD.gn:128,143-148,175-176`；
   `content_main_runner_impl.cc:513-517,966-977,1011,1049,1113,1126` 的 IS_MAC 分支。
4. 0002 两个落点平台中立：`chrome_content_browser_client.cc:4551`、`:4885-4888`、
   `:4907`、`:4967-4970`；函数体内未见 IS_MAC；无 mac `.mm` 覆盖该文件。
5. 0003 落点在 mac 编译：`chrome/browser/BUILD.gn:1264,1273-1277`（`!is_android`）。
6. mac 有第二转发通道未被 0003 覆盖：`process_singleton_mac.mm:16-58,62-82`；
   `process_singleton_posix.cc` 内无 IS_MAC（mac 复用 posix 实现）。
7. `IsMultipleProfileCheckNeeded()` 在 mac 返回 false（`process_singleton.h` IS_MAC 分支）。
8. 0003 的 App-V 防护在 mac 为死代码，mac 无对称防护（补丁 :24-31）。
9. blink 生效链无 mac 分支：`web_view_impl.cc:799-800,830,835,840-841,845-846,866-867`；
   `navigator_events.cc:24-31,125-128`（无 IS_MAC）；
   `media_values.cc:348-372`（无平台分支）；`media_query_evaluator.cc`（无 IS_MAC）；
   `device_capabilities.cc:158-159,228-235`；`web_preferences.h:372-412`。
10. **mac 上 `device_capabilities` 服务被排除**：`content/browser/BUILD.gn:2880`
    `if (is_posix && !is_ios && !is_mac)` —— mac/Linux 的 pointer/hover 上游来源不同。
11. `IsSafeProfilePath` 的 Windows 强化在 mac 不生效：`browser_bootstrap.cc:20-33`；
    mac 缺路径校验分支。
12. `profile_bootstrap_state_test.cc` 未被任何 GN target 引用：`integration/BUILD.gn:20`
    只列 `browser_bootstrap_unittest.cc`。
13. `//chrome/browser` 挂载点在 `source_set("core")`（`BUILD.gn:829` + 补丁 0004:7），
    与被改文件所在 target 一致，无反向依赖环（`integration/BUILD.gn:3` 注释、无 chrome/browser deps）。

---

## 9. 建议的最小 mac 补丁增量（不改行为，仅补门禁；供后续批次决策）

1. `integration/browser_bootstrap.cc`：新增 `#if BUILDFLAG(IS_MAC)` 路径校验分支
   （拒绝 `/Volumes/*` 网络/DMG 挂载、校验父目录链非 symlink、可选 `FSIOC`/st_dev 判定本卷）。
2. `patches/0003`：追加对 `chrome/browser/process_singleton_mac.mm` 的 managed 门禁
   （或在 `ProcessProtocolHandling`/`WaitForAndForwardOpenURLEvent` 入口拒绝带 wire switch 的转发），
   并在文档 §Singleton 边界里把 mac 列为**独立通道**而非"posix 同路"。
3. `patches/0004`/`integration/BUILD.gn`：把 `profile_bootstrap_state_test.cc` 接入 test target；
   并在 mac 上决定 `//content/public/common` 去留。
4. `docs/browser-bootstrap.md`：把"Windows/Linux 专项验证"扩展为"Windows/Linux/macOS"，
   并显式记录 mac 的 AppleEvent 转发通道与 `device_capabilities` 排除这一差异。

---

## 10. 本审计未做的事

- 未构建、未运行、未 `gn gen`/`gn check`（本机无 mac toolchain，且任务禁止构建）。
- 未修改任何文件除本报告外；未做 git 提交。
- 未在 mac 硬件上复现任何行为；本报告所有 mac 结论均为固定源码静态阅读产物。
- 未覆盖第二批面（Screen/DPR/UA-CH/WebGPU/字体/Canvas/Audio），
  除 §5.2 顺带记录的 `media_values.cc:412/:456` IS_MAC 证据可供第二批起步参考。
