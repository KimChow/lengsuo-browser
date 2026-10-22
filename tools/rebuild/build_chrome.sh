#!/bin/bash
# Apply our patches to the synced Chromium tree and build chrome.
#
# Run only after tools/rebuild/fetch_chromium.sh has produced the pinned tree
# and synced its dependencies. Every stage is receipt-guarded, so re-running
# resumes instead of starting over.
#
#   usage: build_chrome.sh [patch|gen|build|all]     (default: all)
set -u

ROOT=${LENGSUO_ROOT:-/home/ubuntu/lengsuo-browser-dev}
SRC=$ROOT/src
DEPS=$ROOT/depot_tools
RECIPE=$ROOT/recipe
REVIEWS=$ROOT/reviews
OUT=$SRC/out/lengsuo
LOG=$REVIEWS/rebuild.log

# Must match baseline.json / fetch_chromium.sh; stage_clean refuses to touch a
# tree that is not on this revision.
REV=0e66b94549a8bd85f03fbdf189b752d6e7b63c32

# 代理只给「确实需要出网的那几条命令」用，**不导出 http_proxy/https_proxy**。
#
# ⛔ 绝不能把 LLM 网关端口（18080）当代理：本机 18080 是 codex-as-api 网关，
#    一旦 export 出去，本 shell 里启动的 claude / Claude Agent SDK 会把它当
#    HTTP 代理，按 RFC 9112 的 absolute-form 发请求行 ⇒ 网关 500 Invalid URL。
#    详见 fetch_chromium.sh 同一处的完整说明。
PROXY=${LENGSUO_PROXY:-http://192.168.1.151:7897}

# ⛔ 硬闸：代理**不得**指向 LLM 网关端口。18080 上是 codex-as-api 网关，
#    把它当 HTTP 代理会让本 shell 里启动的 claude 全部 500（见上）。
case "$PROXY" in
    *:18080|*:18080/*|*://127.0.0.1:18080*|*://localhost:18080*)
        echo "FATAL: PROXY=$PROXY 指向 LLM 网关端口 18080，会导致 claude 全部 500。" >&2
        echo "       出网代理请用 Clash（192.168.1.151:7897）或自建隧道端口，勿用 18080。" >&2
        exit 2
        ;;
esac

export GIT_TERMINAL_PROMPT=0
export DEPOT_TOOLS_UPDATE=0

mkdir -p "$REVIEWS"
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

require_deps() {
    if ! receipt_ok chromium_deps; then
        say "FATAL: dependencies are not synced; run fetch_chromium.sh sync first"
        exit 1
    fi
    if [ ! -d "$SRC/.git" ]; then
        say "FATAL: no source tree at $SRC"
        exit 1
    fi
}

stage_recipe() {
    say "== recipe checkout =="
    if [ -d "$RECIPE/.git" ]; then
        # github.com is reachable directly; the proxy is only needed for
        # googlesource, and routing github through it is slower and flakier.
        # ⛔ 用 `-c http.proxy=`（显式置空）而不是 `env -u http_proxy`：
        #    后者只能清掉**环境变量**，清不掉 git global config 里的
        #    http.proxy（47 上已配 http://192.168.1.151:7897），等于没绕过。
        if ! ( cd "$RECIPE" && git -c http.proxy= -c https.proxy= \
                fetch -q origin main && \
                git -c http.proxy= -c https.proxy= \
                reset -q --hard origin/main ); then
            say "FATAL: could not update recipe; refusing to build from a stale patch set"
            receipt recipe 1
            return 1
        fi
        receipt recipe 0
        say "recipe updated to $(git -C "$RECIPE" rev-parse --short HEAD)"
        return 0
    fi
    # Our own repository carries the patches and the lock file.
    if ! git -c http.proxy= -c https.proxy= \
            clone --depth 1 https://github.com/KimChow/lengsuo-browser.git "$RECIPE"; then
        receipt recipe 1
        say "FATAL: recipe clone failed"
        return 1
    fi
    receipt recipe 0
    say "recipe cloned"
    return 0
}

stage_clean() {
    say "== reset tree to pinned baseline =="
    local actual
    actual=$(git -C "$SRC" rev-parse HEAD)
    if [ "$actual" != "$REV" ]; then
        say "FATAL: HEAD is $actual, expected $REV"
        receipt chromium_clean 1
        return 1
    fi
    # patch_apply is not atomic: a failed run leaves earlier patches applied,
    # and the next run then reports "does not apply cleanly" on a later patch.
    # Discard tracked edits and our own untracked source tree before applying.
    git -C "$SRC" checkout -- .
    rm -rf "$SRC/lengsuo"
    local dirty
    dirty=$(git -C "$SRC" status --porcelain | grep -v '^??' | wc -l)
    receipt chromium_clean $([ "$dirty" -eq 0 ] && echo 0 || echo 1)
    say "clean rc=$? dirty_files=$dirty"
    [ "$dirty" -eq 0 ]
}

stage_patch() {
    say "== apply patches =="
    if receipt_ok chromium_patched; then say "already patched"; return 0; fi
    ( cd "$RECIPE" && python3 -m tools.patch_apply \
        --source "$SRC" --repository "$RECIPE" \
        --lock "$RECIPE/baseline.json" --apply )
    local rc=$?
    receipt chromium_patched $rc
    say "patch rc=$rc"
    return $rc
}

stage_gen() {
    say "== gn gen =="
    if receipt_ok chromium_gen; then say "already generated"; return 0; fi
    mkdir -p "$OUT"
    cp "$RECIPE/tools/rebuild/args.gn" "$OUT/args.gn"
    ( cd "$SRC" && PATH="$DEPS:$PATH" with_proxy "$DEPS/gn" gen "$OUT" )
    local rc=$?
    receipt chromium_gen $rc
    say "gen rc=$rc"
    return $rc
}

stage_build() {
    say "== ninja chrome =="
    if receipt_ok chromium_built; then say "already built"; return 0; fi
    # autoninja keeps the machine responsive; -j is left to its own sizing.
    ( cd "$SRC" && PATH="$DEPS:$PATH" with_proxy "$DEPS/autoninja" -C "$OUT" chrome )
    local rc=$?
    receipt chromium_built $rc
    say "build rc=$rc"
    return $rc
}

require_deps

case "${1:-all}" in
    recipe) stage_recipe ;;
    patch)  stage_recipe && stage_clean && stage_patch ;;
    gen)    stage_gen ;;
    build)  stage_build ;;
    all)    stage_recipe && stage_clean && stage_patch && stage_gen && stage_build; say "ALL rc=$?" ;;
    *) say "unknown stage $1"; exit 2 ;;
esac
