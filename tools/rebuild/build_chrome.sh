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
SRC=$ROOT/chromium-src
DEPS=$ROOT/depot_tools
RECIPE=$ROOT/recipe
REVIEWS=$ROOT/reviews
OUT=$SRC/out/lengsuo
LOG=$REVIEWS/rebuild.log

# Same proxy the fetch used: the host has no direct route to googlesource.
PROXY=${LENGSUO_PROXY:-http://127.0.0.1:18080}
export http_proxy=$PROXY https_proxy=$PROXY
export GIT_TERMINAL_PROMPT=0
export DEPOT_TOOLS_UPDATE=0

mkdir -p "$REVIEWS"
exec >>"$LOG" 2>&1

say() { echo "[$(date -u +%H:%M:%S)] $*"; }
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
        git -C "$RECIPE" fetch -q origin main && git -C "$RECIPE" reset -q --hard origin/main
        receipt recipe 0
        say "recipe updated"
        return 0
    fi
    # Our own repository carries the patches and the lock file.
    git clone --depth 1 https://github.com/KimChow/lengsuo-browser.git "$RECIPE"
    local rc=$?
    receipt recipe $rc
    say "recipe clone rc=$rc"
    return $rc
}

stage_patch() {
    say "== apply patches =="
    if receipt_ok chromium_patched; then say "already patched"; return 0; fi
    ( cd "$RECIPE" && python3 tools/patch_apply.py \
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
    ( cd "$SRC" && PATH="$DEPS:$PATH" "$DEPS/gn" gen "$OUT" )
    local rc=$?
    receipt chromium_gen $rc
    say "gen rc=$rc"
    return $rc
}

stage_build() {
    say "== ninja chrome =="
    if receipt_ok chromium_built; then say "already built"; return 0; fi
    # autoninja keeps the machine responsive; -j is left to its own sizing.
    ( cd "$SRC" && PATH="$DEPS:$PATH" "$DEPS/autoninja" -C "$OUT" chrome )
    local rc=$?
    receipt chromium_built $rc
    say "build rc=$rc"
    return $rc
}

require_deps

case "${1:-all}" in
    recipe) stage_recipe ;;
    patch)  stage_recipe && stage_patch ;;
    gen)    stage_gen ;;
    build)  stage_build ;;
    all)    stage_recipe && stage_patch && stage_gen && stage_build; say "ALL rc=$?" ;;
    *) say "unknown stage $1"; exit 2 ;;
esac
