# W5 任务：catalog 槽位指纹面设计（GPU/WebGL、Canvas、Audio、Fonts、ClientRects）

你是 lengsuo-browser 内核仓的开发工人。你的分支：`worker/w5-slot-faces-design`。
只在本分支提交，不 push，不 merge main。产出为设计文档，不改集成代码
（Chromium 固定树只读研究：`/home/ubuntu/lengsuo-browser-dev/src`）。

## 背景

LBPF1 wire 为 7 个指纹面预留池槽位（canonical 顺序）：
`audio, canvas, client_rects, fonts, gpu, ua, webgl`（slots[0..6]，
见 `native/profile_wire.h`）。slot 值（1..1048576）由 catalog 解析为
具体值；catalog v1（LBPC1，`docs/catalog-contract.md`）只实现了 `ua`
face。ua 面由 W1 在 0006 落地。本任务设计其余 6 个 face 的：
catalog 扩展格式 + browser 注入点 + 测试计划。

## 必读

`AGENTS.md`；`docs/catalog-contract.md`；`docs/batch2-screen-dpr-design.md`
（机制选择格式）；`docs/batch3-ua-uach-design.md`（出口行号核实格式）；
155 固定树（只读）。

## 每个面必须核实并设计（文件:行号）

对每个面回答三件事：JS/CSS 出口在哪、renderer/browser 侧数据源头在哪、
推荐覆盖点（单点优先，论证为什么管住所有出口）。

1. **gpu/webgl**：`WEBGL_debug_renderer_info` 的 UNMASKED_VENDOR/RENDERER、
   `WebGLRenderingContext.getParameter`、WebGPU `requestAdapterInfo`；
   browser 侧 GPU 信息来源（`gpu::GPUInfo`，`content::ContentBrowserClient::
   GetGPUInfo?` / `GpuDataManagerImpl`）；canvas 2d 也会用 GPU 字符串吗。
   注意：换 GPUInfo 是否影响合成器行为（风险项标注）。
2. **canvas**：像素级 noise（value noise/种子注入）的注入点——
   `CanvasRenderingContext2D` 的 `getImageData`/`toDataURL`/`toBlob`
   readback 路径（`canvas_async_blob_creator`/`image_data`）；
   `noise_seed`/`noise_version` 已在 wire（`ParsedProfile.noise_seed`），
   设计 noise 如何用种子确定性地应用（禁随机）。
3. **audio**：`AudioContext`/`OfflineAudioContext` 指纹
   （`audio_buffer` readback 路径，`blink::AudioBuffer`）。
4. **fonts**：`document.fonts.check`、`FontFaceSet`、measure-text 探测；
   覆盖点在 font service / `blink::FontCache`？browser 侧可控性评估
   （fonts 大概率需要 renderer 侧白名单机制——如实说明难度）。
5. **client_rects**：`getBoundingClientRect`/`getClientRects` 的亚像素
   noise 注入点（`Element::getClientRects` → layout tree；noise 与
   canvas 同种子体系是否可行）。

## catalog 扩展设计（LBPC2 提案）

- 每 face 的条目格式（对齐 LBPC1 的 TLV 风格与字符集纪律）：
  gpu 需要哪些字符串字段；canvas/audio/client_rects 需要什么参数
  （强度/位深/种子派生规则——种子从 wire noise_seed 派生，catalog 只给
  策略参数，避免值池）；fonts 是白名单列表还是池引用。
- 条目与 wire slots 的绑定：face 名 + slot 号 → 解析函数返回该面配置。
- 非伪造红线：catalog 不携带的 face 一律上游行为；仓内不出现真实
  值池，测试全合成值。

## 产出

`docs/batch5-slot-faces-design.md`（中文，格式对齐前几批）：
逐面出口核实（行号）→ 覆盖点选择 → LBPC2 格式定义 → 分批实现排序建议
（哪个面最独立、风险最低先行）→ 测试先行计划（单测 + headless 探针矩阵）。
明确「本批不做」。提交到本分支，message 中文。
