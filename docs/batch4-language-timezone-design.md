# 第四批设计：language / timezone 画像覆盖机制

状态：**设计中（本节随核实进度更新）**。产出为覆盖点设计定稿，不改集成
代码；Chromium 155 固定树只读研究。wire（LBPF1）已带 `language`
（可打印 ASCII，≤35）与 `timezone`（≤64）字段，但注入点此前因
WebPreferences 无语言字段而撤销（`reviews/build-b6.log`，
batch2 文档末节定论）。本文给出唯一推荐覆盖点与论证。

## 1. language：出口核实（文件:行号，155 固定树）

### 1.0 mojom 字段位置修正

TASK.md 写的 `content/public/common/renderer_preferences.mojom` 在 155
固定树不存在；`RendererPreferences` 结构定义在
`third_party/blink/public/mojom/renderer_preferences.mojom:18`，字段
`string accept_languages;`（逗号分隔语言列表）在 **:126**（已读行确认）。
研究范围（content/renderer、third_party/blink、network service）填充与
消费链核实中。

### 1.1 RendererPreferences 字段与 browser 侧填充链

（后台核实中，先记录已确认锚点）

### 1.2 renderer 侧消费：navigator.language(s) 取值链

（后台核实中）

### 1.3 网络侧：HTTP Accept-Language 头取值来源

（后台核实中）

### 1.4 devtools 侧存在性证据

`setLocaleOverride`（`third_party/blink/renderer/core/inspector/
inspector_emulation_agent.cc:1052`，转发
`LocaleController::instance().SetLocaleOverride(...)` :1055）说明
渲染侧 locale/语言出口有独立控制器；画像覆盖不能借用该 devtools 通道
（打开 inspector 才生效、语义是调试覆盖）。

## 2. language：机制选择论证

### 2.1 之前编译失败原因定位

web_preferences.h（blink::web_pref::WebPreferences）无语言字段：
`apply_languages` 在 155 的注入结构是 `RendererPreferences`
（renderer_preferences.mojom:126），第一批 WebPreferences 钩子
（`ApplyTouchPreferences`，integration/browser_bootstrap.cc:79）
写不了该值——batch2 末节定论与 build-b6.log 一致（撤销提交 4c13adc）。

### 2.2 唯一推荐覆盖点 + 备选

（待填充）

### 2.3 JS navigator.language(s) 与 HTTP 头是否同源同覆盖

（待填充）

## 3. timezone 梳理

### 3.1 出口核实（blink TimeZoneController / devtools / ICU）

已确认（155 固定树，逐行读取）：

- 控制器定义：`third_party/blink/renderer/core/timezone/timezone_controller.h:47-77`
  （`TimeZoneOverrideResult SetTimeZoneOverride(const String&)` :77，
  所有权模型注释 :47-49——override 由 RAII `TimeZoneOverride` 持有）。
- 实现：`timezone_controller.cc`：
  - `SetTimeZoneOverride` :149-175——单实例互斥（已有 override 时
    :159-162 直接拒绝 `kAlreadyInEffect`）；仅当与 host 时区
    canonical 不等时才改写 ICU 并通知 V8（:165-170）。
  - `SetIcuTimeZoneAndNotifyV8` :226-249——核心落点是
    **`icu::TimeZone::adoptDefault`**（:236），无效 ID 以
    `icu::TimeZone::getUnknown()` 判定拒绝（:232-234），随后向主线程所有
    isolate 发 `DateTimeConfigurationChangeNotification`（:44-47）并遍历
    worker threads（:246-247）。`Date`/`Intl.DateTimeFormat` 的
    resolvedOptions().timeZone 都读 ICU default timezone——即
    **JS 时区出口与 `Intl` 同源，收口于进程内 ICU default**。
  - host 时区来源：构造时 `GetCurrentTimezoneId()` =
    `icu::TimeZone::createDefault()`（:65-69, :97-102）；运行期经
    `device::mojom::blink::TimeZoneMonitor::OnTimeZoneChange` 更新
    （`Init()` :107-120 绑定 mojo client）。
- Init 调用点：`third_party/blink/renderer/core/core_initializer.cc:173`
  `TimeZoneController::Init();`（renderer 主线程 blink 初始化期）。
- devtools 路径：`inspector_emulation_agent.cc:1063`
  `setTimezoneOverride`（重启应用 :269）。与 language 同——是调试通道，
  不是画像通道。
- `ChangeTimeZoneForTesting`（:274-276）走 `OnTimeZoneChange`，测试专用。

待核实：browser 进程（`content::` / `components/`）谁是
`TimeZoneMonitor` 服务方、browser 进程自身 ICU default 的设置时机；
`ContentBrowserClient` 无时区钩子（grep 确认中）。

### 3.2 推荐 browser 侧覆盖点

（待填充——关键问题：时区是 renderer 进程 ICU 状态，非 per-frame
mojo 字段。候选：RendererPreferences 加字段复用现有通道 /
blink::switches 传 renderer 命令行 / devtools 式
TimeZoneController override 由 content 层触发）

## 4. wire 字段 → 注入映射

## 5. 测试先行计划

### 5.1 bootstrap_unittests 用例

### 5.2 真浏览器验证矩阵

## 6. 明确不做清单
