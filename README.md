# Lengsuo Browser

Chromium 内核独立开发仓。客户端运行器、GPUI 与凭证组件属于 `lengsuo-client`。

2026-10-17：已在研究机 1.47 完成 Linux Chromium 编译，接入画像加载、
Touch 与 Screen/DPR；Windows 本机亦完成完整 chrome 构建（VS2026 +
SDK 28000），原生接入单测两平台同套件 5/5 通过，Linux 与 Windows
真浏览器冒烟均通过（正/负/无参数路径）。
这不是完整指纹浏览器；UA/UA-CH、language、WebRTC 等其余指纹面、
运行器与安装验收仍未完成。详见 [原生接入记录](docs/browser-bootstrap.md)
及 [Screen/DPR 记录](docs/batch2-screen-dpr-design.md)。

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
隔离目录进行；Windows 实际构建和运行另行验收。发布签名服务尚未确定。

GitHub Actions 在 Linux/Windows 检查支持组件与测试，不构建整个 Chromium。
原生解码器接口、验证范围与尚缺内容见 [原生验证记录](native/VERIFICATION.md)。
