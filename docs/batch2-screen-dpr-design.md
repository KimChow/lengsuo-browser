# 第二批设计：Screen/DPR 画像覆盖机制

状态：**已实现并在 Linux 与 Windows 真浏览器验证**（Linux 2026-10-16，
Windows 2026-10-17）。0005 补丁在固定树干净应用；bootstrap_unittests
5/5 通过（两平台同套件）；headless 实测：golden 画像下
`screen 1920x1080 / dpr 1 / depth 24`（Windows 冒烟
`{"mtp":0,"w":1920,"h":1080,"dpr":1,"cd":24}`），无画像 `800x600` 上游
行为不变。macOS 验证仍待平台构建。

## 已验证证据（1.47）

- `kernel-0005-check.log`：git apply --check 通过后应用。
- `build-b4.done` rc=0 + `unittests-b4.log`：5/5 通过。
- `chrome-b5.done` rc=0：增量重建。
- headless 实测（reviews/browser-smoke.log 追加）：
  有画像 `{"mtp":0,"sw":1920,"sh":1080,"dpr":1,"cd":24}`；
  无画像 `{"mtp":0,"sw":800,"sh":600,"dpr":1,"cd":24}`。

## 机制选择：browser 侧 WebScreenInfo 供给点覆盖

核实结论（screen-dpr NOTES §4.1–4.2）：

- JS `screen` 全部出口（width/height/avail*/colorDepth/pixelDepth/
  isExtended）经 `Screen::GetRect`/`GetScreenInfo`
  （`screen.cc:172-197`），最终来自 `ChromeClient::GetScreenInfos`。
- CSS `device-width/height`（`media_values.cc:165-187`）与
  `CalculateDevicePixelRatio`（`:195-196` → `LocalFrame::DevicePixelRatio`）
  走 `ChromeClient::GetScreenInfo`（单值）。
- 两条路径在 renderer 侧汇聚于 `WebScreenInfo`，其数据源头是
  **browser 进程**的 `RenderWidgetHostViewBase`（content 层）经 mojo 推送。

因此选 **browser 侧覆盖**：在 `RenderWidgetHostViewBase` 的
WebScreenInfo 产出点替换为画像值。一处覆盖同时满足：

1. JS screen 与 CSS media query 读同一画像（同一 WebScreenInfo）。
2. DPR（`device_scale_factor`）一并覆盖，保留页面 zoom（zoom 在
   blink 侧叠加，不在 WebScreenInfo 生成点）。
3. renderer 不读文件、不新增 mojom——复用现有 WebScreenInfo 通道，
   与第一批"通过现有传播链路"的纪律一致。

## 画像值接入

- `//lengsuo/integration:bootstrap` 本就是 browser-only target，
  新增 `ApplyProfileScreenInfo(display::ScreenInfo*)`：
  `BrowserProfile()` 为空时不动（保持上游）；命中时写
  `rect/available_rect/device_scale_factor(=dpr_milli/1000)/depth`。
- 可用区语义：画像无独立 avail 信息，`available_rect = rect`
  （在 NOTES 标注为语义决定，需产品确认）。
- 与第一批相同：无画像 → 上游行为不变；不伪造 HDR/extended 等
  画像未覆盖字段。

## 补丁面与单测（草案，测试先行）

- `patches/0005-content-screen-profile.patch`：content 层
  render_widget_host_view_base（+ 必要的 screen_infos 产出点）
  调用 lengsuo 覆盖；BUILD 依赖 `//lengsuo/integration:bootstrap`。
- 单测：content_unittests 侧验证"无画像不改写 / 有画像全字段改写 /
  dpr_milli 整数还原"；集成侧进 `bootstrap_unittests`。
- 明确不做：inspector device-metrics override 与画像的优先级仲裁
  本批不实现（devtools 打开时以 inspector 为准需要专项验证，
  NOTES 已列为未验证项）。

## ua-uach 面的接续：被 catalog 解析层阻塞（2026-10-16 定论）

ua-uach 笔记核实（1.47 固定树）确认了覆盖单点在 browser 进程：
`ChromeContentBrowserClient::GetUserAgentOverride()`（约 :2009）与
`GetUserAgentMetadata()`（约 :3125）。

但当前 LBPF1 wire 中 UA 只是池槽位（`slots[5]`），具体 UA 字符串与
品牌/版本列表由 catalog 解析；解码器明确不解析 catalog
（native/VERIFICATION.md），且仓纪律禁止伪造品牌/版本值池。
因此 **UA/UA-CH 覆盖在 catalog 解析契约建立前不能实现**——
提前实现等于伪造。待办：catalog 值解析契约（runner 提供经授权的
本地 catalog，browser 校验后注入两出口），再落地 0006。

wire 中已具备的 `language` 同样不是小步（2026-10-16 撤销验证）：
`accept_languages` 位于 **RendererPreferences**（155 的 WebPreferences
无语言字段），其桌面填充链在 profile/l10n 偏好机制内，需要独立的
覆盖点设计与验证——与 UA/UA-CH 同归 catalog/runner 契约批次。
曾尝试在 WebPreferences 钩子写入导致编译失败并已撤销恢复
（reviews/build-b6.log 为证）。

接续（2026-10-16 本地批次）：catalog 值解析契约已定稿并实现 LBPC1
解码器（`docs/catalog-contract.md`，Windows MSVC 本地红→绿通过），
UA/UA-CH 注入（0006）的阻塞点解除；注入落地、catalog 加载命令行契约、
language 覆盖点设计与 1.47 复核仍未完成。
