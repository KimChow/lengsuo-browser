# Canonical wire decoder verification

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

Not yet evidence: 1.47 g++/ASan/UBSan rerun, GN source_set compilation,
Chromium-tree integration. The refactor of profile_wire.cc onto wire_reader.h
is covered by its unchanged 270-assertion suite locally; Linux rerun still
pending for both components.
