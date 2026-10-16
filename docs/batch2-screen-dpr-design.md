# 第二批设计：Screen/DPR 画像覆盖机制

状态：设计定稿，补丁未写、未验证。证据来源：1.47 固定源码树
（`0e66b945`）上 screen-dpr/ua-uach 两份核实笔记（batch2-work/）。

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

## ua-uach 面的接续

ua-uach NOTES（3.4KB）确认了 navigator_ua.cc / worker 三类 worker
的接入点存在；其补丁同样由本仓接手起草，风格对齐 0002。
