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
        # github.com is reachable directly; the tunnel proxy is only needed for
        # googlesource, and routing github through it is slower and flakier.
        if ! ( cd "$RECIPE" && env -u http_proxy -u https_proxy \
                git fetch -q origin main && \
                git reset -q --hard origin/main ); then
            say "FATAL: could not update recipe; refusing to build from a stale patch set"
            receipt recipe 1
            return 1
        fi
        receipt recipe 0
        say "recipe updated to $(git -C "$RECIPE" rev-parse --short HEAD)"
        return 0
    fi
    # Our own repository carries the patches and the lock file.
    if ! env -u http_proxy -u https_proxy \
            git clone --depth 1 https://github.com/KimChow/lengsuo-browser.git "$RECIPE"; then
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
    patch)  stage_recipe && stage_clean && stage_patch ;;
    gen)    stage_gen ;;
    build)  stage_build ;;
    all)    stage_recipe && stage_clean && stage_patch && stage_gen && stage_build; say "ALL rc=$?" ;;
    *) say "unknown stage $1"; exit 2 ;;
esac
