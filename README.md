# Lengsuo Browser

Chromium 内核独立开发仓。客户端运行器、GPUI 与凭证组件属于 `lengsuo-client`。

2026-10-16：用户已确认实施方向。当前仅建立源码基线与验证工具，
尚无完成构建的定制内核，不代表指纹能力或 Windows 产品已实现。

候选源码：Chromium `155.0.8059.85`，完整 commit 见 `baseline.json`。
只维护官方源码基线和自主补丁，不搬运商业浏览器实现。

验证命令：

```text
python3 -m unittest discover -s tests -v
python3 -m tools.baseline --lock baseline.json --source /path/to/chromium-src
```

第二条只证明锁定的 Git 基线对象与 DEPS 内容一致，不证明依赖已同步、
工作树干净、补丁已应用、源码已编译或发行包可信。

不在 OneDrive 工作区下载 Chromium 构建树。开发与测试在研究机 1.47 的
隔离目录进行；Windows 实际构建和运行另行验收。发布签名服务尚未确定。
