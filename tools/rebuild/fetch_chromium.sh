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
SRC=$ROOT/chromium-src
DEPS=$ROOT/depot_tools
REVIEWS=$ROOT/reviews
LOG=$REVIEWS/rebuild.log

TAG=155.0.8059.85
REV=0e66b94549a8bd85f03fbdf189b752d6e7b63c32
SRC_URL=https://chromium.googlesource.com/chromium/src.git
DEPOT_URL=https://chromium.googlesource.com/chromium/tools/depot_tools.git

# git and gclient both honour these; the tunnel is the only route to
# googlesource from this host, so fail fast instead of silently going direct.
PROXY=${LENGSUO_PROXY:-http://127.0.0.1:18080}
export http_proxy=$PROXY https_proxy=$PROXY
export GIT_TERMINAL_PROMPT=0
export DEPOT_TOOLS_UPDATE=0
export GIT_CONFIG_COUNT=3
export GIT_CONFIG_KEY_0=http.version GIT_CONFIG_VALUE_0=HTTP/1.1
export GIT_CONFIG_KEY_1=http.lowSpeedLimit GIT_CONFIG_VALUE_1=1
export GIT_CONFIG_KEY_2=http.lowSpeedTime GIT_CONFIG_VALUE_2=180

mkdir -p "$REVIEWS" "$ROOT"
exec >>"$LOG" 2>&1

say() { echo "[$(date -u +%H:%M:%S)] $*"; }

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
    ( cd "$ROOT" && PATH="$DEPS:$PATH" "$DEPS/gclient" sync \
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
    ( cd "$SRC" && PATH="$DEPS:$PATH" "$DEPS/gclient" runhooks )
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
