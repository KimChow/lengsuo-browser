# Canvas 噪声原生实现验收（2026-10-18）

`native/canvas_noise.{h,cc}` + `native/canvas_noise_test.cc` 已交付并通过
独立金标逐字节验证。**本批只交付纯算法库**；浏览器接线（RendererPreferences/
Canvas readPixels 钩子）仍未开始，不构成防指纹能力验收。

## 交付物

| 文件 | 内容 |
|---|---|
| `native/canvas_noise.h` | 契约 API：`ApplyCanvasNoiseV1` + status/config/result 类型 |
| `native/canvas_noise.cc` | 算法实现（uint64 回绕算术，行主序，每像素固定两次 mix） |
| `native/canvas_noise_test.cc` | 7 组 41 项断言（自包含，风格同 profile_wire_test） |
| `CMakeLists.txt` | `canvas_noise` 库 + `canvas_noise_test` 目标 + CTest 注册 |

## 与参考实现的对齐过程（两个真实分歧，均已按参考裁决）

1. **floor 除法**：参考用 Python `d * alpha // 255`（向负无穷取整），
   C++ `/` 向零取整。负 delta 时二者不同（-1152/255：floor=-5，trunc=-4）。
   修复：显式 floor 除法。
2. **derive 末尾 mix**：参考 `derive()` 返回 `mix(hi, lo)`——派生本身含
   一次额外 mix。初版漏掉导致 v1/v2 从首字节即偏离。修复后 5 个向量
   全部逐字节吻合。

## 测试覆盖（41 项断言，两轮全绿）

| 组 | 断言 |
|---|---|
| 1 | 5 个冻结金标逐字节吻合（图像区）+ padding 哨兵未被触碰 |
| 2 | `enabled=false`：校验仍执行、dst 一个字节不写（0xA5 哨兵） |
| 3 | 版本/几何/缓冲校验路径全部 fail-closed，失败不写 |
| 4 | changed/preserved 计数与冻结向量独立重算一致（v4: 2+2） |
| 5 | in-place（dst==src）与双缓冲输出在图像区逐字节等价 |
| 6 | 重复调用确定性 |
| 7 | 契约属性：每 RGB 通道 |delta| ≤ 13；alpha 任何情况严格保留 |

## 运行证据（研究机 47，`/home/ubuntu/lengsuo-browser-dev/canvas-test`）

```text
g++ -std=c++20 -Wall -Wextra -Werror -pedantic canvas_noise.cc canvas_noise_test.cc -o canvas_test
./canvas_test
PASS 41 checks          # exit 0

g++ -std=c++20 -Wall -Wextra -Werror -pedantic -fsanitize=address,undefined \
    -fno-omit-frame-pointer canvas_noise.cc canvas_noise_test.cc -o canvas_sanitized
./canvas_sanitized
PASS 41 checks          # exit 0, 无 ASan/UBSan 报告
```

调试期间修掉测试自身的两处断言错误（非实现问题）：
- 金标比对误用 0xA5 哨兵预填全缓冲 → padding 区语义应为「不读不写」，
  改为 src 预填图像区 + 哨兵毒化 padding，两件事分开断言；
- in-place 等价误含 padding 区（两模式 padding 各保持其初始值是正确
  行为）→ 收窄到图像区。

## 明确不保证（边界声明）

- 本组件不知道浏览器存在：无 Chromium 注入、无 IPC、无 readPixels 钩子。
  「Canvas 指纹已可扰动」**不成立**——下一步接线批次才涉及。
- 不保证与任何商业实现兼容；金标只钉「与自有 Python 参考逐字节一致」。
- Windows MSVC 未编译（`/W4 /WX` 已在 CMake foreach 中，待 Windows 批次
  实测）；本批证据仅 Linux g++。

## 门禁核对清单

- [x] 测试先失败后转绿（初版三个真实分歧各自以 FAIL 定位后修复）
- [x] 金标复用已冻结字节，测试运行时不调 Python
- [x] 无动态分配、无静态可变状态、无 IO
- [x] 行数：h 57 / cc 150 / test 233，全部 ≤400
- [ ] Windows MSVC 编译（后续批次）
- [ ] 浏览器接线（后续批次，完成后才可谈 Canvas 指纹能力）
