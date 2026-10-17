# Linux 构建树恢复验收（2026-10-17）

验收对象：内核仓 `1185a2ab9eb74021fcdf3419b0e975ac384a5dbd`，
Chromium `155.0.8059.85`，上游 revision
`0e66b94549a8bd85f03fbdf189b752d6e7b63c32`。
本次恢复已有 0001–0007 补丁，不新增指纹能力。

## 恢复与边界

研究机仓外根目录为 `/home/ubuntu/lengsuo-browser-dev`。
`src` 是指向 `chromium-src` 的符号链接；overlay 工具拒绝符号链接祖先
符合安全契约。恢复使用真实目录 `chromium-src`，没有关闭检查。
应用前确认上游 HEAD 与锁定版本一致、跟踪文件无改动；已有 overlay
内容与历史已知版本核对，备份后重新发布，并应用锁定补丁。

恢复收据 `reviews/batch34-linux-reapply2.log` 中 overlay 和 patches
状态均为 `applied`。收据与构建产物留在研究机，不提交原始运行日志。

## 已执行命令与结果

以下命令在真实源码目录执行，均退出 0：

```text
../depot_tools/autoninja -C out/lengsuo bootstrap_unittests chrome
out/lengsuo/bootstrap_unittests --test-launcher-jobs=1
```

- 增量完整 Chrome 构建成功，日志 `reviews/recovered-tree-build.log`，
  `reviews/recovered-tree-build.exit.json` 记录 `rc: 0`。
- Chromium 启动集成测试 8/8 通过。

真实浏览器验收命令（从研究机根目录执行）：

```text
python3 kernel-repo/tests/browser_ua_smoke.py \
  --chrome chromium-src/out/lengsuo/chrome --repo kernel-repo \
  --work reviews/recovered-language-run1 --check-language
```

退出 0，`reviews/recovered-language-run1/summary.json` 记录 `passed: 9`。
覆盖页面、Dedicated Worker 与 HTTP 请求头的 UA/UA-CH、language、
默认 Intl locale；未启用画像时保留上游行为，无效输入退出 5。
完整字段与限制沿用 [UA 记录](batch3-ua-uach-design.md) 和
[语言记录](batch4-language.md)，不扩大验收范围。

同一内核仓版本的辅助回归重新执行：

- `python -m unittest discover -s tests -v`：Linux 39/39 通过；
  Windows 12 项通过、27 项 POSIX 专用测试跳过，均退出 0。
- CMake Release 构建及 CTest：Linux GCC 13、Windows MSVC 2022
  各 2/2 通过。Windows 使用 `vcvars64.bat` 初始化编译环境；
  未初始化时缺少标准库头文件，属于环境失败，不计作通过。

## 未验收内容

本次没有新增 Windows 完整 Chromium 验收，既有结果见上述语言记录。
Timezone 仍只有红相验收脚本，WebRTC schema 与策略实现仍待契约统一、
测试及双平台浏览器验收。Canvas、Audio 等槽位草稿未经本次验收，
不能把工作进程存活、草稿或单元测试通过写成完整产品完成。
