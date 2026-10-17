# Canonical wire decoder verification

Current status (2026-10-17): the sections below retain historical batch
results. Current wire/catalog suites pass on Linux and Windows; Chromium
bootstrap, full builds and UA/language runtime evidence are linked in
[the recovery record](../docs/linux-tree-recovery.md). Earlier statements
that Windows or GN had not run describe their original batch only.

This component decodes LBPF1 only. It does not resolve pool catalog values,
verify signatures or leases, inject Chromium settings, or prove fingerprint
behavior. Language/timezone validation is bounded printable ASCII, not full
BCP47/IANA validation. Slot ranges do not establish catalog membership.

The 147-byte `golden.inc` is copied from GOLDEN_BYTES in the existing Rust
canonical fixture, independently frozen using Python and Rust. No encoder is
used by this decoder's tests.

## Research machine 1.47

Directory: `/home/ubuntu/lengsuo-browser-dev/native-codec-test`.
No local compilation and no Chromium tree modifications were performed.

Red (stub implementation returning nullopt):

```text
g++ -std=c++20 -Wall -Wextra -Werror -pedantic profile_wire.cc profile_wire_test.cc -o codec-test
compile_exit=0
./codec-test
FAIL 1
test_exit=1
```

Green (implemented decoder): same commands with `-DNDEBUG`, compile_exit=0,
`PASS 270 assertions`, test_exit=0. During test development, an incorrect test
fixture slot offset caused FAIL 221; the offsets were corrected against the
frozen fixture before the final successful run.

Sanitizers:

```text
g++ -std=c++20 -Wall -Wextra -Werror -pedantic -fsanitize=address,undefined -fno-omit-frame-pointer profile_wire.cc profile_wire_test.cc -o codec-sanitized
compile_exit=0
./codec-sanitized
PASS 270 assertions
test_exit=0
```

Coverage includes golden fields/seed; every truncated prefix; oversized/trailing
packets; versions/platform; string lengths/control/non-ASCII; numeric bounds,
area overflow risk; mobile touch; slot IDs, count, unknown/duplicate/reordered
names. All rejects return nullopt, with no exposed partial result.

The main thread configured/built CMake Release and passed CTest on 1.47 using
source `/home/ubuntu/lengsuo-browser-dev/kernel-repo` and build directory
`/home/ubuntu/lengsuo-browser-dev/native-cmake-release` before the portability
cleanup below. BUILD.gn has not been executed; Windows has not been tested.
Chromium GN checking, compilation, browser-to-renderer immutable propagation,
value-pool resolution, and Windows browser behavior remain pending.


## Conversion-warning portability preparation

The first compile added `-Wconversion -Wsign-conversion -Werror` to the baseline
flags and exited 1, with actual diagnostics retained in `conversion-red.log`.
Diagnostics were confined to test fixture index, iterator and byte conversions.
The code now uses explicit bounded casts for fixture byte values and lengths,
size_t offsets/widths and explicit iterator difference conversions. Production
wire validation and all 270 test assertions remain unchanged. The source was
expanded to ordinary two-space indentation and braced statement blocks.

Final Release-style command (1.47 native-codec-test):

```text
g++ -std=c++20 -Wall -Wextra -Werror -pedantic -Wconversion -Wsign-conversion -DNDEBUG -O2 profile_wire.cc profile_wire_test.cc -o conversion-test
compile_exit=0
./conversion-test
PASS 270 assertions
test_exit=0
```

Final sanitized command:

```text
g++ -std=c++20 -Wall -Wextra -Werror -pedantic -Wconversion -Wsign-conversion -fsanitize=address,undefined -fno-omit-frame-pointer profile_wire.cc profile_wire_test.cc -o conversion-sanitized
compile_exit=0
./conversion-sanitized
PASS 270 assertions
test_exit=0
```

Actual output is retained in `conversion-green.log` and
`conversion-sanitizer.log`. GCC warning cleanliness is preparation for Windows,
not evidence of an MSVC build or Chromium GN success. No tools were installed.

## Catalog decoder (LBPC1) — Windows local (2026-10-16)

Component: `profile_catalog.{h,cc}` + shared `wire_reader.h` (extracted from
profile_wire.cc; profile_wire behavior unchanged, 270 assertions pass).
Contract and frozen byte layout: `docs/catalog-contract.md`. Golden fixture
(128 bytes) frozen independently with Python; in-test builders must reproduce
it byte for byte (first assertion). Synthetic values only, no value pools.

Host: Windows local build machine, VS2022 BuildTools 17.12.4 (vcvars64),
CMake+Ninja, `/W4 /WX`. Build tree `D:\lengsuo-build\native-catalog`
(OneDrive repo untouched by builds).

Red (stub `DecodeCatalog` returning nullopt; `catalog-red.log`):
compile_exit=0, `profile_catalog FAIL 3`, ctest 1 of 2 failed, rc=8.

Green (`catalog-green.log`): `profile_wire PASS 270 assertions`,
`profile_catalog PASS 192 assertions`, ctest 2/2 passed, rc=0.

Coverage: builder fidelity to frozen golden; every truncated prefix;
oversized/trailing containers; magic and container_version; pool_version
charset/bounds; entry count bounds; unknown face; slot range and duplicates;
ua string bounds/control chars; brand/full list count and field bounds;
empty-model allowance; mobile flag domain; two-entry catalog; pool binding
(CatalogMatchesProfile match/mismatch/empty).

Not yet evidence at the time of that Windows run: 1.47 g++/ASan/UBSan rerun,
GN source_set compilation, Chromium-tree integration. The 1.47 rerun was
completed the same day — see "Research machine 1.47 (catalog + reader)" below.

## Research machine 1.47 (catalog + reader) — g++ rerun (2026-10-16)

Worktree `worker/w2-catalog-sanitize`, directory `native/`. Toolchain
g++ (Ubuntu 13.3.0-6ubuntu2~24.04.1) 13.3.0, x86_64. Builds into
`/home/ubuntu/lengsuo-browser-dev/reviews/build-w2` (outside the repo);
logs in `/home/ubuntu/lengsuo-browser-dev/reviews/`. No repo sources were
modified for this rerun.

Baseline, full warning set (`-Wall -Wextra -Werror -pedantic -Wconversion
-Wsign-conversion -O2 -DNDEBUG -I.`):

```text
g++ ... profile_wire.cc profile_wire_test.cc -o wt-wire
compile_exit=0  ./wt-wire -> PASS 270 assertions  test_exit=0
# catalog-147-wire-baseline.log

g++ ... profile_catalog.cc profile_catalog_test.cc -o wt-catalog
compile_exit=0  ./wt-catalog -> PASS 192 assertions  test_exit=0
# catalog-147-catalog-baseline.log
```

Assertion counts match the Windows MSVC `/W4 /WX` run exactly (270/192);
no platform-dependent code or test changes were needed.

Sanitizers (`-fsanitize=address,undefined -fno-omit-frame-pointer`,
same warning flags, no `-O2 -DNDEBUG`):

```text
g++ ... -fsanitize=address,undefined -fno-omit-frame-pointer \
    profile_wire.cc profile_wire_test.cc -o wt-wire-san
compile_exit=0  ./wt-wire-san -> PASS 270 assertions  test_exit=0
# catalog-147-wire-sanitizer.log

g++ ... -fsanitize=address,undefined -fno-omit-frame-pointer \
    profile_catalog.cc profile_catalog_test.cc -o wt-catalog-san
compile_exit=0  ./wt-catalog-san -> PASS 192 assertions  test_exit=0
# catalog-147-catalog-sanitizer.log
```

No ASan/UBSan diagnostics on any truncated-prefix, oversized, or boundary
input in either suite.

Red-phase confirmation (catalog): the real `profile_catalog.cc` was left
untouched; the test was linked instead against a stub
(`build-w2/catalog_stub.cc`) whose `DecodeCatalog` returns nullopt:

```text
g++ ... build-w2/catalog_stub.cc profile_catalog_test.cc -o wt-catalog-red
compile_exit=0  ./wt-catalog-red -> FAIL 3  test_exit=1
# catalog-147-catalog-red-stub.log
```

With this, the LBPC1/catalog + wire_reader row of the portability matrix was
green on both toolchains at that revision.

## UA resolution and startup binding (2026-10-17)

`ResolveCatalogUa` adds four assertions (catalog total: 196), covering exact
UA-slot selection, wrong pool, missing slot and ambiguous in-memory entries.
MSVC Release CTest passed both native suites. On 1.47 g++ 13, all 196 catalog
assertions passed with conversion warnings and ASan/UBSan enabled. The separate
startup state harness also passed with ASan/UBSan, including exact binding and
one-shot initialization. Tests first failed on missing resolver/startup APIs.

GN `bootstrap_unittests` and complete Linux/Windows `chrome` now compile with
the catalog source set. All eight Chromium bootstrap tests passed on both
platforms, and `gn check out/lengsuo //lengsuo/*` passed on 1.47.
Real-browser evidence and outstanding UA limitations are tracked in
`docs/batch3-ua-uach-design.md`; decoder checks alone do not prove injection.
