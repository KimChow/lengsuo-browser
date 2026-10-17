# LBPF1 schema 2 原生解码与浏览器拒绝边界

2026-10-17。本批合并的是独立解码组件、跨语言金标和启动拒绝门禁，
**不是 WebRTC 防泄露功能**。客户端生产 ProfileSpecV1 API 未改动，
Chromium 的策略注入补丁、网络出口验收仍待完成。

## 格式与兼容

- 魔数仍为 `LBPF1`；大端 u32 schema_version 允许 1、2。
- v1 编码、147 字节合成金标及既有字段校验全部保留。
- v2 在 timezone 文本后、slot_count 前增加一个 u8 策略，合法值 0..3。
  合成 v2 金标共 148 字节；该字典的策略偏移为 65，实际消息应按字段
  解析，不能把合成字典偏移当作所有画像的固定布局。
- `ParsedProfile::webrtc_policy` 表示该字段；v1 为 nullopt，v2 必须显式
  给出合法值。未知版本、非法策略、缺字段、所有截断前缀及尾随字节拒绝。
- 已有 4096 字节上限、字符串、数值、平台和七槽位顺序校验不变。

策略语义与后续网络门禁见[契约复核](webrtc-contract-review.md)。
policy 2 仍允许非代理 UDP，不能把这个字段或解码成功当作防泄露证明。

## 启动边界

当前浏览器只接受 schema 1。`ProfileBootstrapState::Initialize` 在独立
解码成功后继续检查 schema_version：v2 不发布画像、返回失败、不能重试，
实际 Chrome 退出 5。这样不会出现“接受显式策略但静默忽略”的情况。
原生消费端可测试 v2 格式；开放浏览器 v2 需另批完成 RendererPreferences
注入、origin 覆盖规则、mDNS/ICE/STUN/TURN 等验收，再移除门禁。

## 金标与自动化

`native/golden_v2.inc` 是从显式假字段独立构造的格式金标。
Python 与 Rust 编码器逐字节复现 v1/v2 C++ 夹具；Rust 是测试编码器，
不代表已更新客户端生产者。

```text
cmake -S . -B build -DCMAKE_BUILD_TYPE=Release
cmake --build build --config Release --parallel 2
ctest --test-dir build -C Release --output-on-failure
python tests/check_golden_parity.py
```

对拍脚本在临时目录编译 Rust，所有子进程有超时；不修改仓内夹具。
GitHub Actions 在 Linux/Windows 执行 CTest 和独立对拍，未构建整个 Chromium。

## 实证与红相

1. 旧主线解码器链接新增 v2 测试：编译成功、测试退出 1，拒绝合法 v2。
2. 候选解码器加启动拒绝测试、尚未加门禁：GN 编译成功、测试退出 1，
   复现浏览器发布 v2 画像的问题；加入显式拒绝后转绿。
3. Linux GCC 与 Windows MSVC Release CTest 各 3/3；wire 299、
   catalog 196、v2 独立测试 218 项断言通过。
4. 独立 Python/Rust 对拍两平台各 6/6。Linux 最终合并快照三个测试的
   ASan/UBSan 重新构建并全部通过。
5. 两平台完整 Chrome、启动集成 9/9、真实 v2 拒绝（退出 5）及 v1
   UA/language 回归 9/9 通过。仅 headless、合成画像与 loopback 流量。

Linux 证据在 `/home/ubuntu/lengsuo-browser-dev/reviews/merge-verification/`：
`red-receipt.json`、`green-receipt.json`、`bootstrap-tests.log`、
`schema2-browser-reject.log`、`ua-language/summary.json`。
Windows 证据在 `D:/lengsuo-build/merge-verification/`，包含构建日志、
启动测试、v2 拒绝及 v1 回归摘要。构建产物和原始日志不提交。
