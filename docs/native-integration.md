# Chromium 155 原生接入边界

状态：固定源码定位已完成；overlay 与补丁应用工具已实现并在 1.47 隔离目录
通过全部单测；对真实固定源码树的 GN 校验、编译与浏览器运行尚未完成。
源码 revision：`0e66b94549a8bd85f03fbdf189b752d6e7b63c32`。

工具链顺序（在固定源码副本上）：

```text
python3 -m tools.overlay    --source <src> --repository <repo> --lock baseline.json --apply
python3 -m tools.patch_apply --source <src> --repository <repo> --lock baseline.json --apply
```

`tools.patch_apply` 校验 baseline 声明的补丁清单与 `patches/` 目录一一
对应、逐文件 SHA-256 与声明一致、HEAD/origin/tag/VERSION/DEPS 与锁一致，
然后按文件名顺序应用；若全部补丁已可逆应用则报告 already-applied。
应用要求工作树无 tracked 改动；apply-check 全通过才开始真正应用。
它不放置 overlay 源文件（那是 `tools.overlay` 的职责），不执行 GN，
不编译，不运行浏览器，也不证明依赖已同步。

## 上游符号

| 面 | 固定 revision 中的位置 | 接入约束 |
|---|---|---|
| Screen | `third_party/blink/renderer/core/frame/screen.cc`，`Screen::GetRect` / `GetScreenInfo` | 不能只替换 getter |
| CSS screen | `third_party/blink/renderer/core/css/media_values.cc`，`CalculateDeviceWidth/Height` | 与 JS screen 同一画像 |
| DPR | `core/frame/local_frame.cc`，`LocalFrame::DevicePixelRatio` | 保留页面 zoom，核对 inspector override |
| Touch | `core/events/navigator_events.cc`，`NavigatorEvents::maxTouchPoints` | 从 Settings 读取，不另改 getter |
| Touch 设置 | `core/exported/web_view_impl.cc`，`ApplyWebPreferences` | 同步 pointer/hover/touch detection |
| WebGPU | `modules/webgpu/gpu_adapter.cc`，`CreateAdapterInfoForAdapter` | adapter 和 device 共用信息生成路径 |
| WebGPU device | `modules/webgpu/gpu_device.cc`，`GPUDevice::Initialize` | 会再次创建 adapterInfo，不能漏掉 |
| UA-CH | `core/frame/navigator_ua.cc`，`NavigatorUA::userAgentData` | 不绕过高熵值权限策略 |
| Worker UA | `core/workers/worker_global_scope.cc` | 独立核对 dedicated/shared/service worker |

上述缩写 `core/`、`modules/` 均相对于 `third_party/blink/renderer/`。
不伪造 GPU 原生 features/limits；画像与实际可用能力不兼容时必须拒绝或报告。

## Browser 到 renderer

1. 受管运行器完成授权、内核包校验与环境网络隔离门禁。
2. Browser 解析并冻结无凭据的原生画像；renderer 不读取画像文件或服务端。
3. Touch 复用现有 WebPreferences 链路，在 embedder 中配置一致的触摸属性。
4. Screen 建立专门的 web-exposed 覆盖；真实 compositor/input 设备尺度保留。
5. 新增字段通过受控 common/mojom 类型传播，不在 renderer 临时解析任意字符串。
6. HTTP Client Hints、document 与 worker 的 metadata 必须保持一致。

`ChromeContentBrowserClient::OverrideWebPreferences` 位于
`chrome/browser/chrome_content_browser_client.cc:4551–4904`；自主适配应放在
extra parts 之后，避免被覆盖。还需检查 `OverrideWebPreferencesAfterNavigation`。

早期加载候选为 `ChromeBrowserMainParts::PreEarlyInitialization`，在
BrowserProcess 创建前返回明确非零错误，而非崩溃。该位置不证明所有早期网络
尚未开始，因此不能替代运行器先建立的网络阻断。第二次启动的 singleton 转发
必须校验同一环境，防止将另一画像送入已有进程。

## GN 与 JSON API

155 的 `chrome/browser/BUILD.gn` 已拆分：`.cc` 在 `source_set("core")` 编译，
`source_set("browser")` 主要聚合依赖。新增自主 target 不得只接入 header-only
target，亦不得引入反向依赖环。最终以固定源码的 `gn check` 和编译为准。

`base::JSONReader::ReadDict` 返回 `std::optional<base::DictValue>`，须显式传
`base::JSON_PARSE_RFC`；不沿用旧版本 `base::Value::Dict` 示例。
`ReadFileToStringWithMaxSize` 限制大小，不提供文件读取墙钟超时。
运行器应先把配置放在受管本地目录，拒绝网络路径，且校验解析失败不能继续启动。

## 第一批原生验收

- 主框架、同源 iframe、跨源 OOPIF、popup、导航、BFCache 与 prerender。
- Screen/CSS device-width/height/resolution/DPR，与页面 zoom 80/100/125/200%。
- Windows 显示缩放 100/150/200%、跨真实显示器移动、点击坐标与截图/渲染。
- Touch 0/1/多点，与 pointer/any-pointer/hover/any-hover 一致。
- HTTPS 自建站 UA 与 Sec-CH-UA 头、JS 高熵值、Accept-CH 与跨源权限策略。
- WebGPU adapter.info/device.adapterInfo、真实 render/compute、fallback/设备丢失。
- 同画像重启、异画像隔离、无画像保持上游行为；升级不改变兼容画像身份。

## 官方固定源码

- [Screen](https://chromium.googlesource.com/chromium/src/+/0e66b94549a8bd85f03fbdf189b752d6e7b63c32/third_party/blink/renderer/core/frame/screen.cc)
- [WebPreferences embedder](https://chromium.googlesource.com/chromium/src/+/0e66b94549a8bd85f03fbdf189b752d6e7b63c32/chrome/browser/chrome_content_browser_client.cc)
- [GPUAdapter](https://chromium.googlesource.com/chromium/src/+/0e66b94549a8bd85f03fbdf189b752d6e7b63c32/third_party/blink/renderer/modules/webgpu/gpu_adapter.cc)
- [JSONReader](https://chromium.googlesource.com/chromium/src/+/0e66b94549a8bd85f03fbdf189b752d6e7b63c32/base/json/json_reader.h)
