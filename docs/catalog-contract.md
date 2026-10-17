# catalog 值解析契约（LBPC1）

状态：**解码器已实现并在本地 Windows（MSVC /W4 /WX）测试通过**（2026-10-16）：
`profile_catalog` 192 断言 + 重构后 `profile_wire` 270 断言不变，红→绿证据见
`native/VERIFICATION.md`。1.47 的 g++/sanitizer 复核与 browser 注入（0006）
尚未执行，不得据此宣称 UA/UA-CH 覆盖已可用。

## 背景

第二批定论（batch2 文档）：LBPF1 wire 的 UA 字段只是池槽位（`slots[5]`），
具体 UA 字符串与品牌/版本由 catalog 解析；解码器不解析 catalog，仓纪律禁止
伪造值池。因此 UA/UA-CH 覆盖被 catalog 契约阻塞。本契约即该前置：
runner 提供经授权的本地 catalog 文件，browser 校验结构与绑定后注入两个出口
（`GetUserAgentOverride` 与 `GetUserAgentMetadata`）。

## 容器格式 LBPC1（冻结）

全部大端；文本为 2 字节长度 + 内容；两套字符集：identifier（ASCII 字母
数字、`-`、`_`）与 printable（32..126）。任何畸形输入整体拒绝（nullopt），
不产出部分解析结果；容器上限 4096 字节。

```text
"LBPC1"                    5 字节魔数
container_version          u32，当前必须为 1
pool_version               identifier，1..64
entry_count                u16，1..16
每条目：
  face                     identifier，1..12，v1 只接受 "ua"
  slot                     u32，1..1048576（与 wire 槽位同域），条目间不得重复
  ua_string                printable，1..512，禁止 CR/LF/控制字符
  brand_count              u8，1..8；随后每对：
    brand name             printable，1..32
    brand version          printable，1..32
  full_version_count       u8，0..8；随后同结构对
  platform                 printable，1..32
  platform_version         printable，1..64
  architecture             printable，1..32
  bitness                  printable，1..8
  model                    printable，0..128（唯一允许为空的文本字段）
  mobile                   u8，0 或 1
EOF                        不允许尾随字节
```

## 绑定与授权边界

- `CatalogMatchesProfile`：catalog 与画像的 `pool_version` 必须相等才可
  配套注入；空 pool_version 一律不匹配。
- `ResolveCatalogUa`：仅解析 `profile.slots[5]` 对应的唯一条目；错池、
  缺失或重复匹配均返回空指针。返回值借用 catalog，使用期间须保持其
  生命周期与不可变性。原生单测 196 项断言，Windows MSVC Release 已通过。
- catalog 内容不含凭据；签名授权、catalog 成员验证与环境网络隔离仍由
  生产 runner 负责（与画像 wire 同一边界）。browser 端只做结构校验与
  pool 绑定，不校验来源真实性。
- 非伪造纪律：后续注入只写 catalog 实际携带的字段；未携带的字段保持
  上游行为，不编造默认值。仓内不得出现商业浏览器值池，测试一律使用
  合成值（golden 夹具为 `Synth/1.0` 体系，Python 独立冻结）。

## 实现位置

- `native/profile_catalog.{h,cc}`：LBPC1 解码器（复用 `wire_reader.h`，
  与 LBPF1 同一有界读取纪律）。
- `native/wire_reader.h`：自 profile_wire.cc 抽取的共享游标；`profile_wire`
  270 断言原样通过，证明重构无行为变化。
- GN 侧 `native/BUILD.gn` 的 `profile_catalog` source_set 已进入两平台
  Chromium bootstrap 单测编译，8/8通过；Linux 完整 chrome 编译通过。

## 后续（本契约解锁的工作）

1. 0006 补丁已在固定树接通 UA/UA-CH 两出口，加载与路径纪律对齐
   `--lengsuo-profile-wire`。无 catalog 保持上游；显式错误或缺槽位 catalog
   拒启，详见 `docs/batch3-ua-uach-design.md` 的实测范围。
2. catalog 仍只携带 UA 面；其余维度的类型、加载与一致性验收待完成。
3. language 的 RendererPreferences/profile-l10n 覆盖点设计（独立小批）。
4. 两平台完整浏览器已通过同批9条 UA 运行用例，完整指纹与扩展上下文
   验收仍待完成；组件单测不能代替这些运行证据。
