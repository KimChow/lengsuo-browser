# 时区校验与 WebRTC 偏好辅助函数（2026-10-17）

本批是可测试的接入前置，不开放浏览器 schema 2，不改变现有画像启动行为。
时区渲染覆盖、实际 WebRTC 策略注入及网络防泄露验收仍待完成。

## 时区输入校验

`IsValidProfileTimezone(std::string_view)` 拒绝空、超过 64 字节、控制字符、
嵌入 NUL、非 ASCII、未知时区及非系统的自定义数字偏移。使用锁定的
Chromium ICU `TimeZone::getCanonicalID` 与 system-ID 标记；支持 ICU 已知
系统时区及其别名（含 UTC），不是手写 IANA 表或默认时区回退。

测试覆盖 UTC、New York、London、Etc/GMT-8，以及未知/空/空白/控制/
非 ASCII/NUL/过长输入。初始全 false 实现真实编译成功，合法时区用例
失败；实现后转绿。此函数目前未接入启动拒绝或 Date/Intl 渲染覆盖。

## WebRTC 偏好辅助函数

`ApplyWebRtcPolicy` 只操作传入的 RendererPreferences，不接通运行时：

- nullopt 保持所有既有值；显式 0..3 以 checked switch 映射枚举。
- 显式画像策略优先：清除 per-origin 策略覆盖及本地 IP/mDNS 豁免表，
  保留 UDP 端口区间。无效内存枚举不修改任何字段。
- 返回值表示是否实际变化；重复应用幂等。profile-aware wrapper 在
  无画像或 v1 无策略时保持原值。

测试从非空覆盖表和豁免表开始，覆盖 0..3、无效 4/128/255、端口保留、
无画像及幂等。先用原实现链接新测试复现缺少辅助函数的红相，再执行候选。
修正了测试中的 optional/nullptr 比较与 Chromium 禁止的裸数组索引；
改用 nullopt 与 `std::array::at`，未放宽偏好行为断言。

## 合并快照验证

两平台完整 Chrome 构建和启动集成测试 14/14 通过。Linux GN header
dependency check 通过。旧 9 项测试保留；新增 1 项时区、4 项 WebRTC。

```text
autoninja -C out/lengsuo bootstrap_unittests chrome
out/lengsuo/bootstrap_unittests --test-launcher-jobs=1
gn check out/lengsuo //lengsuo/*
```

Linux 证据：
`/home/ubuntu/lengsuo-browser-dev/reviews/helper-merge-verification/` 下的
`build.log`、`tests.log`、`gn.log`、`receipt.json`。
Windows 证据：`D:/lengsuo-build/helper-merge-verification/tests.log`；
完整构建日志同步保存在该目录。研究机逐项红/绿收据保留于仓外
`reviews/recovery-gates/`，验证器总会还原共享树并确认原基线通过。

已有 [schema 2 浏览器拒绝门禁](webrtc-schema2-native.md) 保持有效。
policy 2 允许非代理 UDP；单元测试与清空偏好表均不能替代真实出口验证。
