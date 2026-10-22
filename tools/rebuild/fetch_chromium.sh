#!/bin/bash
# Rebuild the Chromium source tree for the locked baseline, resumably.
#
# The tree lives outside the repo (it is ~100 GB) and has been lost once
# already, so every stage records a receipt and re-running this script skips
# the stages that already succeeded. Nothing here is destructive: it only adds
# a missing tree and re-checks it against the pinned revision.
#
#   usage: fetch_chromium.sh [stage]        (default: all)
#          stage = deps | chromium | sync | hooks
#
# Pinned baseline: 155.0.8059.85 / 0e66b94549a8bd85f03fbdf189b752d6e7b63c32
# (see docs/browser-bootstrap.md; do not follow a moving main).
set -u

ROOT=${LENGSUO_ROOT:-/home/ubuntu/lengsuo-browser-dev}
SRC=$ROOT/src
DEPS=$ROOT/depot_tools
REVIEWS=$ROOT/reviews
LOG=$REVIEWS/rebuild.log

TAG=155.0.8059.85
REV=0e66b94549a8bd85f03fbdf189b752d6e7b63c32
SRC_URL=https://chromium.googlesource.com/chromium/src.git
DEPOT_URL=https://chromium.googlesource.com/chromium/tools/depot_tools.git

# 代理只给「确实需要出网的那几条命令」用，**不导出 http_proxy/https_proxy**。
#
# ⛔ 绝不能把 LLM 网关端口（18080）当代理：本机 18080 是 codex-as-api 网关，
#    一旦 export 出去，本 shell 里启动的 claude / Claude Agent SDK 会把它当
#    HTTP 代理，按 RFC 9112 的 absolute-form 发请求行（整条 URL 放进 request
#    line）。网关不认这种形式 ⇒ 落到 codex 兜底分支 ⇒ codexUrl() 拼出
#    `http://127.0.0.1:<port>http://127.0.0.1:18080/...` ⇒ ERR_INVALID_URL ⇒
#    客户端收到 500 Invalid URL。表现是「网关日志里出现
#    http://localhosthttp//127.0.0.1:18080」——那是网关日志前缀
#    `http://localhost` 与 absolute-form URL 拼接后的显示假象，不是配置被写坏。
#
# 逐条指定代理（git -c http.proxy= / curl -x）即可出网，且不污染任何子进程
# 环境变量。47 的 git global 已配 http.proxy=http://192.168.1.151:7897，
# 与此默认值一致。
PROXY=${LENGSUO_PROXY:-http://192.168.1.151:7897}

# ⛔ 硬闸：代理**不得**指向 LLM 网关端口。18080 上是 codex-as-api 网关，
#    把它当 HTTP 代理会让本 shell 里启动的 claude 全部 500（见上）。
#    与其等到几百条请求失败后才从网关日志里倒推，不如在这里直接拒绝启动。
case "$PROXY" in
    *:18080|*:18080/*|*://127.0.0.1:18080*|*://localhost:18080*)
        echo "FATAL: PROXY=$PROXY 指向 LLM 网关端口 18080，会导致 claude 全部 500。" >&2
        echo "       出网代理请用 Clash（192.168.1.151:7897）或自建隧道端口，勿用 18080。" >&2
        exit 2
        ;;
esac

export GIT_TERMINAL_PROMPT=0
export DEPOT_TOOLS_UPDATE=0
export GIT_CONFIG_COUNT=3
export GIT_CONFIG_KEY_0=http.version GIT_CONFIG_VALUE_0=HTTP/1.1
export GIT_CONFIG_KEY_1=http.lowSpeedLimit GIT_CONFIG_VALUE_1=1
export GIT_CONFIG_KEY_2=http.lowSpeedTime GIT_CONFIG_VALUE_2=180

mkdir -p "$REVIEWS" "$ROOT"
exec >>"$LOG" 2>&1

say() { echo "[$(date -u +%H:%M:%S)] $*"; }

# 在**当前 shell 的子进程内**临时注入代理变量，然后执行后面的命令。
# 作用域只覆盖这条命令及其子进程，父 shell 与之后的任何命令都不受影响。
#
# ⚠ 这是本脚本唯一允许出现代理 env 的地方。绝不能 `export http_proxy=...`
#   到脚本顶层：那样本 shell 里之后启动的 claude / Claude Agent SDK 会继承，
#   把 18080（LLM 网关）当 HTTP 代理 ⇒ absolute-form 请求行 ⇒ 网关 500。
with_proxy() {
    http_proxy="$PROXY" https_proxy="$PROXY" "$@"
}

receipt_ok() { [ -f "$REVIEWS/$1.exit.json" ] && grep -q '"exit_code": 0' "$REVIEWS/$1.exit.json" 2>/dev/null; }
receipt() { printf '{"exit_code": %s, "stage": "%s", "finished": %s}\n' "$2" "$1" "$(date +%s)" > "$REVIEWS/$1.exit.json"; }

preflight() {
    say "== preflight =="
    local free_gib
    free_gib=$(df --output=avail -BG "$ROOT" | tail -1 | tr -dc '0-9')
    say "free space: ${free_gib}GiB"
    if [ "${free_gib:-0}" -lt 120 ]; then
        say "FATAL: need >=120GiB free, have ${free_gib}GiB"
        exit 1
    fi
    if ! curl -s -o /dev/null --max-time 10 -x "$PROXY" "$SRC_URL"; then
        say "FATAL: proxy $PROXY cannot reach $SRC_URL"
        exit 1
    fi
    say "proxy ok"
}

stage_depot() {
    say "== depot_tools =="
    if [ -x "$DEPS/gclient" ]; then say "already present"; receipt depot_tools 0; return 0; fi
    git -c http.proxy="$PROXY" clone --depth 1 "$DEPOT_URL" "$DEPS"
    local rc=$?
    receipt depot_tools $rc
    say "depot_tools rc=$rc"
    return $rc
}

stage_chromium() {
    say "== chromium clone ($TAG) =="
    if [ -d "$SRC/.git" ]; then say "repo already present"; receipt chromium_clone 0; return 0; fi
    # blob:none keeps the clone small; blobs are fetched in the hydrate stage.
    git -c http.proxy="$PROXY" clone --depth 1 --filter=blob:none --no-checkout \
        --single-branch --branch "$TAG" "$SRC_URL" "$SRC"
    local rc=$?
    receipt chromium_clone $rc
    say "clone rc=$rc"
    return $rc
}

stage_hydrate() {
    say "== hydrate blobs =="
    if receipt_ok chromium_hydrate; then say "already hydrated"; return 0; fi
    git -C "$SRC" -c http.proxy="$PROXY" fetch --progress --refetch --no-filter \
        --depth 1 origin "refs/tags/$TAG"
    local rc=$?
    receipt chromium_hydrate $rc
    say "hydrate rc=$rc"
    return $rc
}

stage_checkout() {
    say "== checkout working tree =="
    if receipt_ok chromium_checkout; then say "already checked out"; return 0; fi
    local actual
    actual=$(git -C "$SRC" rev-parse HEAD)
    if [ "$actual" != "$REV" ]; then
        say "FATAL: HEAD is $actual, expected $REV"
        receipt chromium_checkout 1
        return 1
    fi
    # Materialise files from the index. No lazy fetch: blobs are local by now.
    GIT_NO_LAZY_FETCH=1 git -C "$SRC" read-tree HEAD &&
        GIT_NO_LAZY_FETCH=1 git -C "$SRC" checkout-index --all
    local rc=$?
    receipt chromium_checkout $rc
    say "checkout rc=$rc"
    return $rc
}

stage_sync() {
    say "== gclient sync (deps) =="
    if receipt_ok chromium_deps; then say "already synced"; return 0; fi
    # managed=false: the pinned checkout above is authoritative; gclient only
    # brings in the DEPS graph.
    if [ ! -f "$ROOT/.gclient" ]; then
        cat > "$ROOT/.gclient" <<EOF
solutions = [
  {
    "name": "src",
    "url": "$SRC_URL",
    "deps_file": "DEPS",
    "managed": False,
    "custom_deps": {},
    "custom_vars": {},
  },
]
EOF
    fi
    # Use the depot_tools wrapper, not \`python3 gclient.py\`: the wrapper runs
    # bootstrap_python3, which the gn wrapper later needs. It also has to run
    # from $ROOT, where .gclient lives — running elsewhere reports
    # "client not configured".
    #
    # ⚠ 代理**只在这个子 shell 里**生效（with_proxy）：gclient 自己不发 http，
    #   是它派生的 git 子进程要出网，所以必须用 env 传代理，不能只靠
    #   `git -c http.proxy=`。而代理 env 一旦 export 到父 shell，本 shell 里
    #   之后启动的 claude / Claude Agent SDK 就会继承它 ⇒ 把 18080 当代理 ⇒
    #   网关 500 Invalid URL。所以作用域必须收死在子 shell 内。
    ( cd "$ROOT" && PATH="$DEPS:$PATH" with_proxy "$DEPS/gclient" sync \
        --nohooks --noprehooks --no-history --jobs 4 \
        --revision "src@$REV" \
        --output-json "$REVIEWS/chromium-deps-solutions.json" )
    local rc=$?
    receipt chromium_deps $rc
    say "sync rc=$rc"
    return $rc
}

stage_hooks() {
    say "== runhooks =="
    if receipt_ok chromium_hooks; then say "already ran"; return 0; fi
    # Same as sync: gclient resolves .gclient from the working directory, and
    # runhooks additionally needs the src solution it was configured with.
    ( cd "$ROOT" && PATH="$DEPS:$PATH" with_proxy "$DEPS/gclient" runhooks )
    local rc=$?
    receipt chromium_hooks $rc
    say "hooks rc=$rc"
    return $rc
}

preflight || exit 1

case "${1:-all}" in
    deps)     stage_depot ;;
    chromium) stage_chromium && stage_hydrate && stage_checkout ;;
    sync)     stage_sync ;;
    hooks)    stage_hooks ;;
    all)
        stage_depot && stage_chromium && stage_hydrate && stage_checkout &&
            stage_sync && stage_hooks
        say "ALL STAGES rc=$?"
        ;;
    *) say "unknown stage $1"; exit 2 ;;
esac
