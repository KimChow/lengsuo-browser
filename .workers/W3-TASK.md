# W3 任务：WebRTC 防泄露指纹面设计（第四批设计定稿）

你是 lengsuo-browser 内核仓的开发工人。你的分支：`worker/w3-webrtc-design`。
只在本分支提交，不 push，不 merge main。本任务以**设计与证据**为产出，
不改集成代码、不动 Chromium 树文件（只读研究可以）。

## 目标

WebRTC 是浏览器指纹/泄露的头号面：ICE host candidates 暴露真实内网 IP、
STUN srflx 暴露公网 IP、mDNS 混淆行为、非代理 UDP 直连绕过网络隔离。
用户要求「各种可能导致泄露用户的逻辑全部处理好」。本批要产出可评审的
设计定稿 `docs/batch4-webrtc-design.md`（中文，结构对齐
docs/batch2-screen-dpr-design.md 与 docs/batch3-ua-uach-design.md）。

## 必读

1. `AGENTS.md`；`docs/browser-bootstrap.md`（第一批 WebPreferences 钩子先例）
2. `docs/batch2-screen-dpr-design.md`（机制选择论证格式：出口核实 → 单点覆盖）
3. `docs/batch3-ua-uach-design.md`（出口行号核实格式）
4. Chromium 155 固定树（只读）：`/home/ubuntu/lengsuo-browser-dev/src`

## 必须逐项在 155 源码核实（带文件:行号引用）

1. `blink::WebRTCIPHandlingPolicy` 枚举全值与语义
   （third_party/blink/public/common/web_preferences/web_preferences.h 或同类）；
   `WebPreferences.webrtc_ip_handling_policy` 字段存在性。
2. renderer 侧消费点：WebRTC 的
   `blink::WebFrameClient`/`peer_connection_dependency_factory` 如何把该策略
   传给 `webrtc::PortConfiguration`/`BasicPortAllocator`（找
   `SetWebRTCIPHandlingPolicy` 或 `rtc_configuration` 的转换处）。
3. mDNS 混淆：`kWebRtcHideLocalIpsWithMdns` feature 现状（155 是否默认开），
   mdns_responder 的 browser 侧归属（`content/browser/webrtc/mdns_responder.cc`）。
4. chrome 层现有设置路径：`chrome/browser/profiles`/`prefs` 里
   `webrtc.ip_handling_policy` pref（kWebRTCIPHandlingPolicy）谁写谁读
   （media/webrtc 相关 pref 服务），确认它最终进 WebPreferences 的链路。
5. leak 向量清单：host/srflx/relay candidate、`RTCPeerConnection.getStats`
   里的本地地址、`network enumeration`（`network_enumeration` 相关 API）、
   WebRTC 的 UDP socket 是否走 browser 网络隔离（network service vs
   独立 rtc socket）——哪些在 `webrtc_ip_handling_policy` 覆盖范围内，
   哪些不在（如 `iceTransportPolicy` 由页面控制的部分）。

## 设计产出要求

- **机制选择**：预期结论是 WebPreferences 单点覆盖（与 Touch 同钩子
  `OverrideWebPreferences`，lengsuo 已有 `ApplyTouchPreferences` 先例）——
  论证它是否同时管住 JS 出口与底层 port allocator；若不够（如 mDNS 或
  network service 层需要 browser 侧额外动作），列出第二覆盖点及理由。
- **画像值来源**：LBPF1 wire（schema 1）**没有** webrtc 字段。给出 wire
  schema 2 演进提案：字段名、取值域（建议直接用 policy 枚举 0..3）、
  解码器兼容策略（v1 画像继续有效，默认策略=？；明确「默认必须是上游
  行为」还是「管理态强制 public_only」——论证并标注为需产品拍板的语义决定）。
  遵守非伪造纪律：不发明枚举外值。
- **leak 收尾清单**：policy 之外仍会泄露的路径（如 mDNS .local 可枚举
  接口数、STUN 服务器可达性探测），逐项标注：本批处理 / 后续批 / 上游
  行为不处理（理由）。
- **测试先行计划**：bootstrap_unittests 用例清单 + 真浏览器验证矩阵
  （headless 下 RTCPeerConnection candidate 观测方式）。

写完后自查：每个论断有 文件:行号；不与已验证批次结论冲突；明确列出
「本批不做」。提交到本分支，message 中文。
