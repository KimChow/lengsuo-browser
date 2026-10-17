# Screen/DPR 与 Touch 实际浏览器回归（2026-10-17）

测试脚本：`tests/browser_screen_touch_smoke.py`。针对已有 0004/0005
画像能力补充可重复运行的回归，本批不修改生产补丁。
Chromium 基线为 `baseline.json` 固定的 `155.0.8059.85`。

## 观测与结果

Linux 1.47 与本机 Windows 使用同一份脚本，各 3/3 通过、退出 0。
脚本 SHA256：
`51e847153cd75ac871abc7c0dace2f1c836e3918d08bbe9e3994420d6d68b72b`。

| 画像 | screen.width × height | colorDepth | devicePixelRatio | maxTouchPoints |
| --- | --- | --- | --- | --- |
| 未启用画像（本次观测） | 800 × 600 | 24 | 1 | 0 |
| 合成 golden | 1920 × 1080 | 24 | 1 | 0 |
| 合成 custom | 1366 × 768 | 30 | 1.5 | 2 |

测试不硬编码宿主屏幕：第一行是实际运行观测，两个受管理用例的预期
从各自 wire 读取，并检查与未启用画像的观测、两画像之间存在差异。
测试只使用合成数据、独立临时浏览器目录和 loopback HTTP。

## 命令与证据位置

在 1.47 的 `worktrees/w1-ua-uach` 执行：

```text
python3 tests/browser_screen_touch_smoke.py \
  --chrome /home/ubuntu/lengsuo-browser-dev/chromium-src/out/lengsuo/chrome \
  --repo /home/ubuntu/lengsuo-browser-dev/kernel-repo \
  --work /home/ubuntu/lengsuo-browser-dev/reviews/screen-touch-run2-1792230262
```

该目录 `run.log` 记录 `RC=0`，`summary.json` 为 `passed: 3, ok: true`。
首轮 `screen-touch-run1-1792230106` 同样 3/3 通过；随后去掉导入另一个
CLI 测试模块的副作用，改为直接读取 C++ 合成夹具，第二轮重验通过。

Windows 执行相同脚本：

```text
python D:/lengsuo-build/browser_screen_touch_smoke.py \
  --chrome D:/lengsuo-build/src/out/lengsuo/chrome.exe \
  --repo D:/OneDrive/project/lengsuo-browser \
  --work D:/lengsuo-build/screen-touch-win-validation-1792230056
```

退出 0，运行目录 `summary.json` 同样 3/3、`ok: true`。
39 项辅助测试在 Windows 通过，其中 27 项 POSIX 专用用例跳过。

## 范围限制

只验证 headless 顶层页面的上述五个观测字段。多显示器、窗口移动、
DevTools override、iframe、Worker、实际触摸事件及 GUI 渲染尚未由
本脚本覆盖；不把触点数量覆盖等同于真实触摸设备能力。
UA/language 等历史接续状态以 [UA](batch3-ua-uach-design.md)、
[language](batch4-language.md) 记录为准。
