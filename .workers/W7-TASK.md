# W7 任务：WebRTC 防泄露实现（0009，测试先行）

你是 lengsuo-browser 内核仓的开发工人。你的分支：`worker/w7-webrtc-impl`。

## 输入（就绪状态）

- W3 设计草案已在：`/home/ubuntu/lengsuo-browser-dev/worktrees/w3-webrtc-design/docs/batch4-webrtc-design.md`
  （**只读**，那是 W3 的活动区）。核心结论已定：155 中
  `webrtc_ip_handling_policy` **不在 WebPreferences**，在
  **RendererPreferences**（与 language 同通道）；完整论证、mDNS、
  leak 收尾清单以 W3 分支最终提交为准。
- 你与 W3 并行：做**不依赖设计终稿**的树外部分；每阶段结束前重读一次
  W3 草案，若机制变化则调整。

## 阶段 1（现在就做，树外、worktree 内）

1. wire schema 2：按 W3 草案的演进提案在 `native/profile_wire_test.cc`
   加红用例（schema 2 含 webrtc 字段 + schema 1 完全兼容），然后改
   `native/profile_wire.{h,cc}`。schema 2 的字段名/取值域必须与 W3
   草案一致；若草案尚未写该节，按 `WebRTCIPHandlingPolicy` 枚举
   0..3 + 默认值语义设计并在 NOTES.md 标注待与 W3 对齐。
2. integration 侧：`ApplyProfileWebRtcPolicy`（读写
   RendererPreferences.webrtc_ip_handling_policy 的应用函数，命名可调）
   + `browser_bootstrap_unittest.cc` 红用例 → 绿（单测编译验证见阶段 3）。
3. 全部小步提交。

## 阶段 2：0009 补丁 + 文档（树外）

- `patches/0009-browser-webrtc-policy.patch`（renderer_preferences 填充点，
  位置按 W3 草案行号；先 `git diff` 生成后再走 baseline 重锁流程）。
- 更新设计文档状态行（引用 W3 文档）、README。

## 阶段 3：树上闭环（双重门禁）

仅当同时满足：(a) `reviews/batch34-linux-reapply2.log` 存在且含
`"status": "applied"`（W1 的树恢复标记）；(b) W3 分支已提交设计终稿。
不满足则提交进度后正常退出并注明。树上步骤同 W6 任务书（overlay 重放
含 0009、autoninja bootstrap_unittests、chrome 重建、真浏览器观测矩阵
——离线子集，srflx 标注需授权网络专项）。

## 纪律

AGENTS.md；不动 src 树与其他 worktree；卡住写 NOTES.md 提交退出。
