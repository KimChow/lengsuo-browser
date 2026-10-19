# W2 任务：LBPC1/LBPF1 解码器 1.47 正式复核（g++ + sanitizer）

你是 lengsuo-browser 内核仓的开发工人。你的分支：`worker/w2-catalog-sanitize`。
只在本分支提交，不 push，不 merge main。

## 背景

`native/profile_catalog.{h,cc}`（LBPC1 catalog 解码器）与共享游标
`native/wire_reader.h` 已在 Windows MSVC /W4 /WX 下红→绿通过
（`native/VERIFICATION.md` 有记录），但 **1.47 的 g++/sanitizer 复核缺失**，
不完成不得声明跨平台成立。

## 步骤

1. 在本 worktree 的 `native/` 下用 g++ 编译（严格按 VERIFICATION.md 既有命令风格）：
   - 基线：`g++ -std=c++20 -Wall -Wextra -Werror -pedantic -Wconversion -Wsign-conversion -O2 -DNDEBUG profile_wire.cc profile_wire_test.cc -o wt-wire`（含 golden.inc 所在 include 路径）
   - catalog：同 flags 编 `profile_catalog.cc profile_catalog_test.cc`（include 路径加 `.`，因 test include profile_catalog.h 与 wire_reader.h 同目录）
   - sanitizer 版本：`-fsanitize=address,undefined -fno-omit-frame-pointer`（不带 -O2 -DNDEBUG）
   - 注意 profile_catalog_test.cc 是独立 main，两个测试分开编。
2. 运行并记录：PASS 断言数（预期 wire 270 / catalog 192；若不符，先查原因，
   是平台差异就要修代码/测试并说明，不得改断言凑数）。
3. 红相确认（可选但推荐）：临时把实现换成返回 nullopt 的桩重编出 FAIL，
   记录后恢复——证明测试真的在测实现。
4. 更新 `native/VERIFICATION.md`：追加 "Research machine 1.47 (catalog + reader)" 小节，
   命令、退出码、断言数、日志文件路径（把日志存到 `/home/ubuntu/lengsuo-browser-dev/reviews/catalog-147-*.log`
   并在文档引用）。
5. 提交到本分支（文档+可能的小修复），输出总结。

## 禁止

不改 GN/补丁/集成代码（那是 W1 的活动区）；不动 Chromium 固定树。
如发现 wire_reader/profile_catalog 在 g++ 下的真实编译错误，修代码前先让
测试变红记录，修复后全量重跑两个测试套件。
