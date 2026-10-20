#!/usr/bin/env bash
# Compile and run the L3 storage-layer gtests on the build host.
#
# The Chromium build tree lays our sources out as src/lengsuo/{native,integration},
# so the integration tests include "lengsuo/integration/l3_bootstrap.h". Outside
# that tree those paths do not resolve, so we stage a shadow directory of
# symlinks and point -I at it. The native tests use bare includes and are
# compiled with -I native.
#
# Usage: run_l3_gtest.sh [recipe-dir] [gtest-root]
#   recipe-dir  default: $PWD
#   gtest-root  default: <repo>/../src/third_party/googletest/src/googletest
set -u -o pipefail

REPO="${1:-$PWD}"
GTEST="${2:-}"
if [ -z "$GTEST" ]; then
  for cand in \
    "/home/ubuntu/lengsuo-browser-dev/src/third_party/googletest/src/googletest" \
    "$REPO/third_party/googletest/src/googletest"; do
    if [ -f "$cand/src/gtest-all.cc" ]; then GTEST="$cand"; break; fi
  done
fi
if [ ! -f "$GTEST/src/gtest-all.cc" ]; then
  echo "FATAL: googletest not found; pass <gtest-root> as argv[2]" >&2
  exit 2
fi

STAGE="$(mktemp -d)"
trap 'rm -rf "$STAGE"' EXIT
mkdir -p "$STAGE/lengsuo"
ln -s "$REPO/native" "$STAGE/lengsuo/native"
ln -s "$REPO/integration" "$STAGE/lengsuo/integration"

WORK="$(mktemp -d)"
trap 'rm -rf "$STAGE" "$WORK"' EXIT

CXXFLAGS=(-std=c++20 -Wall -Wextra -Werror -pedantic
          -I "$REPO/native" -I "$STAGE"
          -isystem "$GTEST/include" -isystem "$GTEST")

GTEST_SRC=("$GTEST/src/gtest-all.cc" "$GTEST/src/gtest_main.cc")

# name | extra sources
TARGETS=(
  "storage_pin|native/storage_pin.cc"
  "storage_consistency|native/storage_consistency.cc"
  "cache_record|native/cache_record.cc"
  "extension_pin|native/extension_pin.cc"
  "l3_wire|native/l3_wire.cc native/storage_pin.cc native/storage_consistency.cc native/cache_record.cc native/extension_pin.cc"
  "l3_runtime|native/l3_runtime.cc native/l3_wire.cc native/storage_pin.cc native/storage_consistency.cc native/cache_record.cc native/extension_pin.cc"
  "l3_bootstrap|integration/l3_bootstrap.cc native/l3_wire.cc native/storage_pin.cc native/storage_consistency.cc native/cache_record.cc native/extension_pin.cc"
  "cookie_mirror|native/cookie_mirror.cc native/storage_consistency.cc native/storage_pin.cc native/l3_wire.cc native/cache_record.cc native/extension_pin.cc"
)

FAILED=()
PASSED=()
for spec in "${TARGETS[@]}"; do
  name="${spec%%|*}"
  extra="${spec#*|}"
  srcs=("$REPO/native/${name}_test.cc")
  if [ "$name" = "l3_bootstrap" ]; then
    srcs=("$REPO/integration/l3_bootstrap_test.cc")
  fi
  for s in $extra; do srcs+=("$REPO/$s"); done

  if ! g++ "${CXXFLAGS[@]}" "${srcs[@]}" "${GTEST_SRC[@]}" -pthread \
        -o "$WORK/$name" 2> "$WORK/$name.cc.log"; then
    echo "COMPILE-FAIL  $name"
    sed -n '1,20p' "$WORK/$name.cc.log" | sed 's/^/    /'
    FAILED+=("$name")
    continue
  fi
  if "$WORK/$name" > "$WORK/$name.run.log" 2>&1; then
    summary="$(grep -E '^\[==========\].*ran' "$WORK/$name.run.log" | tail -1)"
    echo "PASS          $name  ${summary:-ok}"
    PASSED+=("$name")
  else
    echo "RUN-FAIL      $name"
    grep -E '^\[  FAILED  \]|^\[==========\]' "$WORK/$name.run.log" | tail -12 | sed 's/^/    /'
    FAILED+=("$name")
  fi
done

echo "----"
echo "passed: ${#PASSED[@]}  failed: ${#FAILED[@]}"
if [ ${#FAILED[@]} -gt 0 ]; then
  echo "FAILED: ${FAILED[*]}"
  exit 1
fi
