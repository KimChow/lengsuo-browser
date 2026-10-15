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
