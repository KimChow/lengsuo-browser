# Decoder source overlay tool

Run from this repository root (Python standard library and Git only):

```text
python3 -m tools.overlay --source /absolute/chromium/src --repository /absolute/lengsuo-browser --lock /absolute/lengsuo-browser/baseline.json
```

The default is a read-only plan. Add `--apply` explicitly to publish the files.
The tool prints a JSON receipt to stdout and returns 1 on failure, 0 on success.
Capture stdout to retain the receipt, including partial failure state.

The fixed mappings place `native/profile_wire.h`, `native/profile_wire.cc`,
`native/BUILD.gn`, `native/golden.inc` and the six integration files
(`browser_bootstrap.{cc,h}`, `profile_bootstrap_state.{cc,h}`,
`browser_bootstrap_unittest.cc`, `BUILD.gn`) into the corresponding `lengsuo/`
files in the Chromium checkout.
The standalone `profile_bootstrap_state_test.cc` is intentionally not mapped:
it only runs in the repository's standalone harness. The GN labels are
`//lengsuo/native:profile_wire` and `//lengsuo/integration:bootstrap`, both
reported in the receipt as `gn_targets`; source paths remain relative to
BUILD.gn, so they need no text substitution. No browser BUILD.gn dependency
or source hook is added by this tool.

The existing baseline inspector verifies origin, full HEAD, release tag,
chrome/VERSION and the committed DEPS digest. The source must be the checkout
root, not a nested directory. Nonempty baseline patch lists are refused: this
tool is an overlay applicator, not a general patch executor. Existing baseline
checks are reused without weakening them.

Each receipt entry contains exact bytes' SHA-256, size, fixed source/destination
paths, and create/created/identical action. Different existing content, modified
tracked target files, path traversal, symlink ancestors and nonregular files are
rejected. Identical existing files are left unchanged for idempotent reruns.

Apply currently requires POSIX no-follow directory APIs; Windows apply is
explicitly rejected before baseline inspection or writes. POSIX fixture tests
skip when that capability is unavailable; the explicit rejection test remains
portable. Windows execution has not been verified. Files are written to
exclusive temporary files in the checked target directory, flushed, then
published by a hard link that cannot overwrite an existing path. POSIX reads
also walk ancestors using no-follow directory descriptors. Ordinary directory
creation may remain after failure. Individual file publication is atomic;
**the group of files is not atomic**. A failure receipt preserves completed
entries; resolve the reported conflict and rerun rather than resetting a tree.
No reset, force operation, network fetch, tool installation or build is run.

This only establishes source-file placement. It does not prove GN checking,
Chromium compilation, browser-to-renderer profile propagation, Windows runtime
behavior, signing, authorization or complete fingerprint compatibility.

## Isolated tests

Tests create their own tiny local Git repositories, tags and fake baseline
metadata. They never modify the real Chromium checkout. Research machine 1.47
working directory: `/home/ubuntu/lengsuo-browser-dev/overlay-tool-test`.

```text
python3 -m unittest discover -s tests -v
```

The initial stub run executed 11 tests and returned 1 (two failures and three
errors). The implemented version passed all 11 with exit 0. Final expanded
coverage passed 15 tests with exit 0 in 1.598 seconds, including CLI defaults and
JSON failures, nested-checkout rejection, publication-time symlink races and
explicit unsupported-platform rejection. Logs remain in that isolated remote
directory as red.log and green.log. One intermediate 14-test run took 61.864
seconds under disk I/O pressure while gclient fetched DEPS-declared testdata;
it was allowed to finish and passed, rather than being restarted.

After moving the suite to `tests/test_overlay.py` for existing CI discovery,
`python3 -m unittest discover -s tests -v` passed all 25 baseline and overlay
tests, exit 0, in 1.419 seconds. Output is remote `discover-green.log`.
Local `python tools/check_line_limit.py`: 21 files checked, zero hard failures.
Windows plan is available as a read-only inspection path, but has not been run;
Windows apply remains explicitly unsupported. No Windows success is claimed.
