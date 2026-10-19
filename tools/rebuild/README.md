# 内核重建

上游源码不放进仓库（约 100 GB），但这套流程必须能从仓库复现。所有阶段幂等，
每步写回执到 `$ROOT/reviews/*.exit.json`，重跑即续跑。

```bash
# 1) 拉源码与依赖（需代理，见下）
tools/rebuild/fetch_chromium.sh all

# 2) 复位 → 应用补丁 → gn gen → 编译
tools/rebuild/build_chrome.sh all
```

环境变量：`LENGSUO_ROOT`（默认 `/home/ubuntu/lengsuo-browser-dev`）、
`LENGSUO_PROXY`（默认 `http://127.0.0.1:18080`）。

## 基线

`155.0.8059.85` / `0e66b94549a8bd85f03fbdf189b752d6e7b63c32`，与
`baseline.json`、`docs/browser-bootstrap.md` 一致。`stage_clean` 会拒绝在
HEAD 不等于该 revision 的树上动手。

## 网络

该机没有直连 `chromium.googlesource.com` 与 `storage.googleapis.com` 的路由
（TLS 被中断），必须走代理。当前做法是 SSH 反向隧道，把本机 Clash 的 7897
映射到目标机 127.0.0.1:18080：

```bash
ssh -N -R 18080:127.0.0.1:7897 <host>
```

`github.com` 可直连，所以 recipe 仓的拉取刻意绕过代理。

## 已知坑（都实际踩过）

| 现象 | 原因 | 处理 |
|---|---|---|
| `client not configured` | 在错误目录跑 gclient | 必须在 `.gclient` 所在目录执行 |
| `python3_bin_reldir.txt not found` | depot_tools 未引导 | `source bootstrap_python3 && bootstrap_python3` |
| 同一 hunk 被应用两次/冲突 | 部分补丁是从"已打补丁的树"冻结的，自带前面补丁的改动 | 用 `apply_hunks.py` 按 hunk 应用，重复的跳过 |
| `patch does not apply cleanly` 于后面的补丁 | `patch_apply` 非原子，失败会留下前面已应用的补丁 | `build_chrome.sh` 的 `stage_clean` 先复位 |
| 补丁摘要校验不过 | Windows 上 Python 写入把 `\n` 变 `\r\n` | 一律 `write_bytes` 写 LF；已加测试 |

## 补丁集的现状（待收敛）

`patches/0001`–`0010` 之间**存在重叠**：`0007-canvas-readback` 与
`0008-fp-overrides` 都包含了 `0002` 已经加过的 include 和 touch 偏好改动，
因为它们是从一棵已经应用过 0002 的树上冻结的。表现是按顺序 `git apply`
必失败。

当前用 `apply_hunks.py` 解决：逐个 hunk 应用，若该 hunk 的**所有新增行**
已经存在于目标文件，就判定为重复并跳过；否则必须应用成功，任何失败都
让整轮失败，不静默放过。

> 这是权宜之计。正确做法是把 0001–0010 重新收敛成一个从干净基线生成的
> 补丁，消除重叠。收敛前不要新增依赖顺序的补丁。
