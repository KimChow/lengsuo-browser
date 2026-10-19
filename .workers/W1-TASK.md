# W1 任务：第三批 0006 UA/UA-CH 画像注入（测试先行）

你是 lengsuo-browser 内核仓的开发工人。你的分支：`worker/w1-ua-uach`（已检出为当前 worktree）。
只在本分支提交，不 push，不 merge main。全部工作在本机（1.47）完成。

## 必读（按序）

1. `AGENTS.md`（仓纪律：测试先失败再实现、文档随代码更新、700 行上限）
2. `docs/batch3-ua-uach-design.md`（本批设计定稿：出口、映射、绑定契约、测试清单）
3. `docs/catalog-contract.md`（LBPC1 解码契约）
4. `native/profile_catalog.h` + `integration/browser_bootstrap.{h,cc}`（现有机制）
5. `docs/browser-bootstrap.md`（第一批加载契约与纪律先例）

## 环境路径

- Chromium 固定树（**只有你允许改动**）：`/home/ubuntu/lengsuo-browser-dev/src`
  （已应用 0001–0005，`out/lengsuo` 已有 gn/ninja 缓存，args 为非 component）
- depot_tools：`source /home/ubuntu/lengsuo-browser-dev/depot_tools/env.bash` 或
  `export PATH=/home/ubuntu/lengsuo-browser-dev/depot_tools:$PATH`
- 审查日志目录：`/home/ubuntu/lengsuo-browser-dev/reviews/`（证据日志放这里，命名 `batch3-*.log`）

## 实施步骤（严格测试先行）

1. **红**：扩展 `integration/browser_bootstrap_unittest.cc`：
   - 合成 LBPC1 catalog（参考 `native/profile_catalog_test.cc` 的构建方式）经
     `--lengsuo-catalog-wire` 加载成功；pool 匹配/不匹配；slot 命中/无命中；
     UA 字符串与 UserAgentMetadata 字段映射逐一断言。
   - 新 API 放 `integration/browser_bootstrap.{h,cc}`：
     `LoadBrowserCatalog(const base::CommandLine&)`（`InitializeBrowserProfile` 成功后调用一次）；
     `ApplyProfileUserAgent()` 返回 `const std::string*`（无命中返回 nullptr）；
     `ApplyProfileUserAgentMetadata(blink::UserAgentMetadata*)`（无命中不改写返回 false）。
   - 先编译失败/断言失败，记录 `reviews/batch3-red.log`。
2. **绿**：实现状态持有（`ProfileBootstrapState` 增加 catalog 可选成员，冻结一次、
   失败不可替换，纪律与 profile 一致：空路径/相对/超长/解码失败/pool 不绑定 → 拒绝启动 rc=5 路径）。
   重跑单测 `autoninja -C /home/ubuntu/lengsuo-browser-dev/src/out/lengsuo bootstrap_unittests`
   然后 `.../out/lengsuo/bootstrap_unittests`，记录 `reviews/batch3-unittests.log`。
3. **0006 补丁**：对固定树生成
   `patches/0006-browser-ua-profile.patch`：
   `chrome_content_browser_client.cc` 的 `GetUserAgent()`（约 :7733）与
   `GetUserAgentMetadata()`（约 :7737）插入 lengsuo 覆盖；无画像/无 catalog/无命中
   一律直通上游。禁止伪造未携带字段（`full_version`/`wow64`/`form_factors` 不写）。
   生成后重锁基线并应用：
   `python3 -m tools.baseline --lock baseline.json --source /home/ubuntu/lengsuo-browser-dev/src`
   然后 `python3 -m tools.overlay --source <src> --repository <本worktree> --lock <worktree>/baseline.json --apply --allow-patched-baseline`
   再 `python3 -m tools.patch_apply --source <src> --repository <worktree> --lock <worktree>/baseline.json --apply`。
   注意：先在固定树 `git checkout -- chrome/` 撤销手工改动再走补丁工具。
4. **重建 + 真浏览器冒烟**：`autoninja -C <src>/out/lengsuo bootstrap_unittests chrome`。
   headless 冒烟（参考 docs/browser-bootstrap.md 的做法）：
   - 无参数：上游 UA。
   - wire+catalog：`navigator.userAgent` 与 `navigator.userAgentData` 与画像一致
     （用 `--headless --dump-dom` + 内嵌 JS 或既有冒烟脚本模式）。
   - pool 不绑定的 catalog：拒绝启动。
   记录 `reviews/batch3-smoke.log`。
5. **文档**：更新 `docs/batch3-ua-uach-design.md`（状态行）、`docs/browser-bootstrap.md`
   （如加载契约有扩展）、`README.md` 状态段（如有必要）。
6. **提交**：分批提交到本分支，message 用中文、说明测试证据。最后输出总结：
   改了什么、红→绿证据路径、遗留问题。

## 禁止

- 不改 `worker/` 其他分支的文件；不 push；不动 `/home/ubuntu/lengsuo-browser-dev/src` 以外
  的其他 Chromium 目录；不伪造任何 UA/品牌值池（测试只用 Synth/1.0 体系合成值）。
- catalog 解码器（native/profile_catalog.cc）如需改动，先在
  `native/profile_catalog_test.cc` 加失败用例再改实现。
