# Lengsuo Browser

Chromium 内核独立开发仓。客户端运行器、GPUI 与凭证组件属于 `lengsuo-client`。

2026-10-17：已在研究机 1.47 完成 Linux Chromium 编译，接入画像加载、
Touch 与 Screen/DPR；Windows 本机亦完成完整 chrome 构建（VS2026 +
SDK 28000），原生接入单测两平台同套件 5/5 通过，Linux 与 Windows
真浏览器冒烟均通过（正/负/无参数路径）。
Screen/DPR、Touch 新增双平台实际浏览器回归，各 3/3 通过，见[测试记录](docs/screen-touch-runtime-verification.md)。
第三批已将 catalog 的 UA/UA-CH 字段接通：两平台完整 chrome 编译及8项
原生接入测试通过，真实页面、Dedicated Worker与HTTP请求头一致性通过。
Linux 构建树恢复后已重新通过完整构建、8 项启动测试和 9 项浏览器验收，
见[恢复验收记录](docs/linux-tree-recovery.md)。
第四批接通画像语言：页面、Dedicated Worker、默认Intl locale与HTTP语言头
两平台实测一致，9条运行用例通过，见[语言记录](docs/batch4-language.md)。
原生解码组件已支持 schema 2 并加入跨语言金标；浏览器暂拒绝 v2，
避免静默忽略未验收的 WebRTC 策略，见[组件验收](docs/webrtc-schema2-native.md)。
Cookie/LocalStorage 目录隔离、重启持久化与清理回归双平台各 10/10 通过，
见[存储回归记录](docs/storage-isolation-runtime.md)。
新增 ICU 时区校验和 WebRTC 偏好辅助函数，双平台启动单测 14/14 通过，
WebRTC运行时接入仍待验收，见[辅助函数记录](docs/browser-preference-helpers.md)。
时区补丁0008已通过Linux和Windows页面与Dedicated Worker的15条用例，含冬夏偏移与非法时区拒启，见[时区记录](docs/timezone-runtime.md)。
SharedWorker和ServiceWorker时区扩展回归亦已双平台通过，见[Worker记录](docs/timezone-worker-contexts.md)。
这不是完整指纹浏览器；UA高熵缺项、语言标签全集、时区扩展上下文、WebRTC等其余指纹面、
运行器与安装验收仍未完成。详见 [原生接入记录](docs/browser-bootstrap.md)
及 [Screen/DPR 记录](docs/batch2-screen-dpr-design.md)。UA/UA-CH 已验证字段
及未覆盖范围见 [第三批记录](docs/batch3-ua-uach-design.md)。

指纹覆盖现状（2026-10-19）：canvas / audio / clientRects / GPU 人脸 / WebGL 参数 /
hardwareConcurrency / deviceMemory / UA / language / timezone 已接线并通过端到端验收；
**字体维度未实现**（slot 已解析、无消费点；根因是 `ParsedProfile` 把它编码成一个档位号，与对标端统一的 `{switcher, value}` 形状对不上）。
落地方案见 [字体维度方案](docs/font-dimension-reference-approach.md)。switch 字段契约见
[指纹 switch 契约](docs/fp-switch-contract.md)，人工验收步骤见
[手工验收手册](docs/manual-acceptance.md)（含 `tests/fp-lab.html` 测试页）。

外显一致性现状（2026-10-19，与官方 Chrome 逐字段比对）：

- **UA-CH 品牌已补齐**：非 branded 构建原本只报 `Chromium`（读 UA-CH 的站点一眼可判
  非官方 Chrome），现经 `native/product_brand.h` + `patches/0012-ua-ch-brand.patch`
  输出 `[Google Chrome, Chromium, Not(A:Brand]`，与官方同形。实测主线程与
  Worker / Service Worker 逐项一致。见[官方对比](docs/official-comparison.md)。
- **WebGL `MAX_*` 与画像一致**：驱动原值即正确值，**不做随机扰动**——真实驱动只报
  2 的幂或固定架构常数，人造值比原值更显眼。见[手工验收手册](docs/manual-acceptance.md) §2。

服务端下发契约（按对标端客户端实证冻结，**待我方拍板**）：字段表、`ua_ch` 陷阱、
R1–R15 校验规则与两套错误码隔离见 [ProfileSpecV1 契约](docs/profile-spec-v1.md)；
GPU 维度细节见 [服务端 GPU 字段需求](docs/server-gpu-fields-request.md)；
三层能力边界（元数据 / 像素 / 时序）见 [GPU 与检测面方案](docs/gpu-and-detection-plan.md)。
⛔ 注意 `60003` 是**同号异义**（对标端=路径不存在，我方=画像矛盾），
书写必须带归属限定语，由 `tests/test_error_code_docs.py` 强制。

项目源码公开可查看；项目许可证尚未确定，不宣称已授予开源许可或
已经符合任何免费代码签名服务资格。Chromium 等上游组件保留各自许可。

候选源码：Chromium `155.0.8059.85`，完整 commit 见 `baseline.json`。
只维护官方源码基线和自主补丁，不搬运商业浏览器实现。

验证命令：

```text
python3 -m unittest discover -s tests -v
python3 -m tools.baseline --lock baseline.json --source /path/to/chromium-src
cmake -S . -B build -DCMAKE_BUILD_TYPE=Release
cmake --build build --config Release --parallel 2
ctest --test-dir build -C Release --output-on-failure
```

基线校验只证明锁定的 Git 基线对象与 DEPS 内容一致，不证明依赖已同步、
工作树干净、补丁已应用、源码已编译或发行包可信。

不在 OneDrive 工作区下载 Chromium 构建树。开发与测试在研究机 1.47 的
隔离目录进行；Windows 已通过本机完整编译和基础运行冒烟，扩展运行矩阵、
安装与升级仍待验收。发布签名服务尚未确定。

GitHub Actions 在 Linux/Windows 检查支持组件与测试，不构建整个 Chromium。
原生解码器接口、验证范围与尚缺内容见 [原生验证记录](native/VERIFICATION.md)。
