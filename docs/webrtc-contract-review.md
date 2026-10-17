# WebRTC schema 2 实施前复核（2026-10-17）

状态：设计复核，尚未实现或冻结 schema 2，不代表防泄露能力已交付。
对照对象为 W3 设计草案与 W7 未提交代码；主线解码器目前只接受 v1。
Chromium 基线沿用 `baseline.json` 的 `155.0.8059.85` 完整 revision。

## 已核实的机制

`third_party/blink/public/mojom/renderer_preferences.mojom:98` 的
`webrtc_ip_handling_policy` 属 RendererPreferences。其后
`webrtc_ip_handling_urls` 注释明确：匹配顶层 origin 的条目优先于单值策略。
`webrtc_local_ips_allowed_urls` 另行控制本地 IP 的 mDNS 豁免。
因此只写单值、保持其他字段不动，不能证明策略实际生效或豁免表为空。
后续必须测试预先存在非空规则的画像环境，并明确受管理画像的优先级。

`third_party/blink/renderer/modules/peerconnection/peer_connection_dependency_factory.cc`
的 1052–1075 行分支表明：

| 策略 | enable_multiple_routes | enable_nonproxied_udp |
| --- | --- | --- |
| 0 default | true | true |
| 1 public and private interfaces | false | true |
| 2 public interface only | false | true |
| 3 disable nonproxied UDP | false | false |

策略 2 还将 `enable_default_local_candidate` 设为 false，但这不能代替
实际 ICE 验收，不能笼统承诺“无 host/srflx 候选”。它仍允许非代理 UDP，
页面使用 STUN 时必须检查 srflx 与真实出口。策略 3 的分支也不等于完整
防泄露证明；TURN、TCP、IPv6、权限与 mDNS 等仍需授权测试网络验证。

## 两份草稿的冲突与实施建议

- W3 建议新 u8 字节位于 timezone 后、slot_count 前；W7 草稿则追加
  在七条 slot 后。目前没有发布的 v2 消费者或跨语言金标，不应混用。
- 建议统一采用 timezone 后的布局，字段名使用
  `webrtc_ip_handling_policy`，值域 0..3。该建议须先由 Rust 生产者、
  C++ 消费者与独立 Python 金标达成逐字节一致，再冻结协议。
- v1 字节与语义继续保留：缺少字段不写入新策略，不静默强制为 2 或 3。
  v2 必须显式携带合法策略，非法、缺失或尾随字节整体拒绝。
- 不把“新版解码器接受合法 v1”误写成失败用例。应测试的是伪造版本号、
  v2 缺字段、畸形字段与额外字节，并继续验证合法 v1 兼容。

现有 LBPF1 金标为 147 字节、8 字节 noise_seed。v2 金标必须从显式
合成字段独立构造，不只给旧字节数组追加或插入一个字节来自证布局。
当前 LBPC1 是有界固定字段结构，未知字段与尾随内容拒绝，不能按
“已有可跳过未知字段的 TLV”方式新增 Canvas/Audio 种子。

## 合并门禁

1. Rust/C++/Python 独立金标与 v1 回归、所有截断前缀、值域和尾随检查。
2. Chromium 注入测试覆盖 v1 不改值、v2 显式 0..3、重复应用及非空 origin
   覆盖规则；明确规则优先级后再测试，不能只断言全新默认空表。
3. 同批补丁、锁定摘要和文档更新；Linux/Windows 编译与真实浏览器验收。
4. SDP、ICE candidate、getStats、STUN/TURN 与实际网络出口证据；离线
   收不到 candidate 不能证明阻断真实网络泄露。

W7 未提交的 schema 与注入代码目前不满足这些门禁，未合并入主线。
