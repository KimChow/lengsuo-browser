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
#
# kernel_pipe needs BoringSSL (AES-256-GCM, SHA-256, HKDF), and so does the
# L3E1 document envelope (AES-256-GCM, PBKDF2, SHA-256). It is found in
# this order: $BORINGSSL_DIR, the Chromium tree's third_party/boringssl, a
# host build cached under ~/.cache/lengsuo-boringssl. The target is skipped
# with a loud message when no BoringSSL is available - it is never silently
# dropped, because "passed 8" must mean the new targets really ran.
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

# BoringSSL: headers and the static lib. A host build is used when the
# Chromium tree has no prebuilt object for this platform.
BORINGSSL_DIR="${BORINGSSL_DIR:-}"
if [ -z "$BORINGSSL_DIR" ]; then
  for cand in \
    "/home/ubuntu/lengsuo-browser-dev/src/third_party/boringssl/src" \
    "$REPO/third_party/boringssl/src"; do
    if [ -f "$cand/include/openssl/aead.h" ]; then BORINGSSL_DIR="$cand"; break; fi
  done
fi
BORINGSSL_LIB=""
if [ -n "$BORINGSSL_DIR" ]; then
  for cand in \
    "$BORINGSSL_DIR/libcrypto.a" \
    "$BORINGSSL_DIR/../../../out/Release/libcrypto.a" \
    "$HOME/.cache/lengsuo-boringssl/libcrypto.a"; do
    if [ -f "$cand" ]; then BORINGSSL_LIB="$cand"; break; fi
  done
fi

# name | extra sources
TARGETS=(
  "storage_pin|native/storage_pin.cc"
  "storage_consistency|native/storage_consistency.cc"
  "cache_record|native/cache_record.cc"
  "extension_pin|native/extension_pin.cc"
  "l3_wire|native/l3_wire.cc native/storage_pin.cc native/storage_consistency.cc native/cache_record.cc native/extension_pin.cc"
  "l3_runtime|native/l3_runtime.cc native/l3_wire.cc native/storage_pin.cc native/storage_consistency.cc native/cache_record.cc native/extension_pin.cc"
  "l3_bootstrap|integration/l3_bootstrap.cc native/l3_wire.cc native/storage_pin.cc native/storage_consistency.cc native/cache_record.cc native/extension_pin.cc"
  "l3_provisioning|native/l3_wire.cc native/storage_pin.cc native/storage_consistency.cc native/cache_record.cc native/extension_pin.cc native/l3_runtime.cc"
  "cookie_mirror|native/cookie_mirror.cc native/storage_consistency.cc native/storage_pin.cc native/l3_wire.cc native/cache_record.cc native/extension_pin.cc"
  "kernel_pipe_frame|native/kernel_pipe_frame.cc"
  "kernel_pipe_json|native/kernel_pipe_json.cc"
  "kernel_pipe_keys|native/kernel_pipe_keys.cc native/kernel_pipe_frame.cc"
  "kernel_pipe_envelope|native/kernel_pipe_envelope.cc native/kernel_pipe_frame.cc native/kernel_pipe_keys.cc"
  "kernel_pipe|native/kernel_pipe.cc native/kernel_pipe_frame.cc native/kernel_pipe_json.cc native/kernel_pipe_keys.cc native/kernel_pipe_envelope.cc"
  "action_table|native/action_table.cc native/kernel_pipe_json.cc"
  "profile_bundle|native/profile_bundle.cc native/kernel_pipe_json.cc"
  "machine_facts|native/machine_facts.cc native/kernel_pipe_keys.cc native/kernel_pipe_frame.cc"
  "l3_envelope|native/l3_envelope.cc"
  "l3_envelope_session|native/l3_envelope_session.cc native/l3_envelope.cc native/kernel_pipe_keys.cc native/kernel_pipe_frame.cc"
  "resp_monitor_config|native/resp_monitor_config.cc native/kernel_pipe_json.cc native/policy_match.cc native/profile_bundle.cc"
  "resp_monitor_body|native/resp_monitor_body.cc"
  "resp_monitor|native/resp_monitor.cc native/resp_monitor_config.cc native/kernel_pipe_json.cc native/policy_match.cc native/profile_bundle.cc"
  "resp_monitor_report|native/resp_monitor_report.cc native/action_table.cc native/kernel_pipe_json.cc"
  # The access policy (task T2). The layers are listed the way they depend
  # on each other: the matchers come first because the rules need the URL
  # validator.
  "policy_match|native/policy_match.cc native/profile_bundle.cc native/kernel_pipe_json.cc"
  "policy_rules|native/policy_rules.cc native/policy_match.cc native/profile_bundle.cc native/kernel_pipe_json.cc"
  "policy_block|native/policy_block.cc native/policy_rules.cc native/policy_match.cc native/policy_report.cc native/profile_bundle.cc native/kernel_pipe_json.cc native/action_table.cc"
  "fp_sampling|native/fp_sampling.cc native/canvas_noise.cc"
  "fp_converge|native/fp_converge.cc native/fp_sampling.cc native/canvas_noise.cc"
  "fp_target|native/fp_target.cc native/fp_target_match.cc native/fp_sampling.cc native/canvas_noise.cc native/kernel_pipe_json.cc native/profile_bundle.cc"
  # The access policy's request-path driver (task 0018). Only the pure half
  # is compiled: the header decides, and the URLLoaderThrottle in the .cc is
  # Chromium's type and only ever links in a real build.
  "policy_throttle|native/policy_block.cc native/policy_rules.cc native/policy_match.cc native/policy_report.cc native/profile_bundle.cc native/kernel_pipe_json.cc native/action_table.cc"
  # The per-URL target entry (task 0019). It sits on top of the profile
  # perturbation, which owns the font tables, so those come along.
  "fp_overrides|native/fp_overrides.cc native/fp_target.cc native/fp_target_match.cc native/fp_sampling.cc native/canvas_noise.cc native/kernel_pipe_json.cc native/profile_bundle.cc native/font_family_traits.cc native/font_metrics.cc"
  # The response monitor's fan-out tee (task 0017). The judge the card names
  # lives here: what came out of the page's side is the bytes that went in.
  # It needs the capture it fills.
  "resp_tee|native/resp_tee.cc native/resp_monitor_body.cc"
  # The response monitor's response-path driver (task 0017). As with
  # policy_throttle, only the pure half is compiled here: the header decides,
  # and the URLLoaderThrottle in the .cc is Chromium's type and only ever
  # links in a real build.
  "resp_throttle|native/resp_monitor.cc native/resp_monitor_config.cc native/kernel_pipe_json.cc native/resp_monitor_body.cc native/resp_tee.cc native/resp_monitor_report.cc native/action_table.cc native/policy_match.cc native/profile_bundle.cc"
)

FAILED=()
PASSED=()
for spec in "${TARGETS[@]}"; do
  name="${spec%%|*}"
  extra="${spec#*|}"
  srcs=("$REPO/native/${name}_test.cc")
  # Integration-level tests live next to the code they exercise.
  case "$name" in
    l3_bootstrap) srcs=("$REPO/integration/l3_bootstrap_test.cc") ;;
    l3_provisioning) srcs=("$REPO/integration/l3_provisioning_impl_test.cc") ;;
    policy_throttle) srcs=("$REPO/integration/policy_throttle_test.cc") ;;
    resp_throttle) srcs=("$REPO/integration/resp_throttle_test.cc") ;;
  esac
  for s in $extra; do srcs+=("$REPO/$s"); done

  # The pipe targets that use BoringSSL. When it is unavailable the target is
  # reported as failed, not skipped: a silently missing target would make
  # "passed 8" a lie. The frame and JSON layers deliberately do not need it.
  # The L3E1 document envelope needs it too (AES-256-GCM, PBKDF2, SHA-256).
  target_flags=("${CXXFLAGS[@]}")
  target_link=(-pthread)
  case "$name" in
    kernel_pipe|kernel_pipe_envelope|kernel_pipe_keys|l3_envelope|l3_envelope_session|machine_facts)
      if [ -z "$BORINGSSL_DIR" ] || [ -z "$BORINGSSL_LIB" ]; then
        echo "COMPILE-FAIL  $name (BoringSSL not found; set BORINGSSL_DIR)"
        FAILED+=("$name")
        continue
      fi
      target_flags+=(-I "$BORINGSSL_DIR/include")
      target_link=("$BORINGSSL_LIB" -pthread)
      ;;
  esac

  if ! g++ "${target_flags[@]}" "${srcs[@]}" "${GTEST_SRC[@]}" "${target_link[@]}" \
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
