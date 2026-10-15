# Lengsuo Browser

Chromium 内核独立开发仓。客户端运行器、GPUI 与凭证组件属于 `lengsuo-client`。

2026-10-16：已建立源码基线验证工具与 C++20 原生画像线格式解码器。
尚无完成构建的定制内核，不代表指纹注入或 Windows 浏览器产品已实现。

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
