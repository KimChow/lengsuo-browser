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
`LENGSUO_PROXY`（默认 `http://192.168.1.151:7897`，即本机 Clash）。

> ⛔ `LENGSUO_PROXY` **不得**指向 `18080`。18080 是 LLM 网关
> （codex-as-api），把它当 HTTP 代理会让本 shell 里启动的 claude /
> Claude Agent SDK 全部 500。两个脚本都有硬闸，指向 18080 直接 `exit 2`。

## 基线

`155.0.8059.85` / `0e66b94549a8bd85f03fbdf189b752d6e7b63c32`，与
`baseline.json`、`docs/browser-bootstrap.md` 一致。`stage_clean` 会拒绝在
HEAD 不等于该 revision 的树上动手。

## 网络

该机没有直连 `chromium.googlesource.com` 与 `storage.googleapis.com` 的路由
（TLS 被中断），必须走代理。出网代理用本机 Clash：

```bash
# 47 上已配好 git global，二者一致
git config --global http.proxy http://192.168.1.151:7897
```

代理**逐条指定**，不导出到脚本顶层：

- `git -c http.proxy="$PROXY" clone/fetch`（拉 googlesource）
- `curl -x "$PROXY"`（探活）
- `git -c http.proxy= -c https.proxy=`（显式置空，走直连的 github.com）

### ⛔ 为什么不能 `export http_proxy=...`

代理 env 一旦 export 到脚本顶层，**本 shell 里之后启动的任何进程都会继承**，
包括 claude 与 Claude Agent SDK。它们会把 `ANTHROPIC_BASE_URL` 当成要发给代理的
绝对 URL，按 RFC 9112 的 absolute-form 写请求行：

```
POST http://127.0.0.1:18080/v1/messages?beta=true HTTP/1.1
```

网关只认 origin-form（`POST /v1/messages`），absolute-form 会落到 codex 兜底
分支，`codexUrl()` 拼出 `http://127.0.0.1:<port>http://127.0.0.1:18080/...`
⇒ `ERR_INVALID_URL` ⇒ 客户端收到 **500 `Invalid URL`**。

网关日志里会看到 `http://localhosthttp//127.0.0.1:18080/...` —— 那是网关日志
前缀 `http://localhost` 与 absolute-form URL 拼接后的**显示假象**，不是配置被写坏；
真正的错误信息是 `Invalid URL`。

所以脚本用 `with_proxy()` 把代理变量**收在单个子 shell 内**：

```bash
with_proxy() { http_proxy="$PROXY" https_proxy="$PROXY" "$@"; }
```

父 shell 与之后的命令都不受影响。

> 旧做法是 SSH 反向隧道 `ssh -N -R 18080:127.0.0.1:7897 <host>`，把 18080
> 兼作隧道端口与代理端口。这条路已废弃：**18080 现在是 LLM 网关端口**，
> 隧道一旦与网关同机就必然撞车。

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
