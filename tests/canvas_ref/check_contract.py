"""Contract invariants of the canvas_noise v1 *Python reference only*.

Scope guard: this checks tests/canvas_ref/gen_canvas_ref.py against
docs/canvas-component-contract.md. It exercises NO C++ code and is NOT
a browser, Windows, or anti-fingerprinting acceptance test. Frozen
golden fixtures do not exist in-tree yet, so nothing here compares
against committed bytes.

Checks, on the 5 synthetic vectors and fixed-seed repeats:
  1. determinism: same inputs -> byte-identical output (3 repeats);
  2. fully-transparent pixels (alpha==0) preserved byte-for-byte;
  3. alpha never rewritten; row padding [width*4, stride) untouched;
  4. input immutability: apply_v1 leaves src unchanged;
  5. src_bytes salt generator is algorithm-independent & stable;
  6. changed-pixel sanity: at least one vector both preserves
     transparency and alters some opaque pixel (v4);
  7. plan-A bound: every single-channel perturbation stays within
     [-13, 13] (contract prose and pseudocode now agree on the mild
     variant d*alpha/255; the retired (v2 & 0xFF) factor would break
     this and is rejected here).
Exits nonzero on any failure.
"""
import importlib.util
import os
import sys

HERE = os.path.dirname(os.path.abspath(__file__))
spec = importlib.util.spec_from_file_location(
    "gen_canvas_ref", os.path.join(HERE, "gen_canvas_ref.py"))
ref = importlib.util.module_from_spec(spec)
spec.loader.exec_module(ref)

FAILURES = []


def check(cond, label):
    print(("PASS " if cond else "FAIL ") + label)
    if not cond:
        FAILURES.append(label)


def pixels(w, h, stride):
    for row in range(h):
        for col in range(w):
            yield row * stride + col * 4


cases = list(ref.build_cases())
check(len(cases) == 5, "exactly 5 synthetic vectors defined")

for name, seed, slot, w, h, stride, src, expected in cases:
    # 1. determinism under fixed seed: repeated runs are byte-identical.
    again = [ref.apply_v1(seed, slot, w, h, stride, src) for _ in range(3)]
    check(all(a == expected for a in again), f"{name}: repeat runs identical")

    # 2/3. transparency and alpha invariants, per pixel.
    all_alpha0_kept = True
    alpha_never_changed = True
    for p in pixels(w, h, stride):
        if src[p + 3] == 0:
            all_alpha0_kept &= src[p:p + 4] == expected[p:p + 4]
        alpha_never_changed &= src[p + 3] == expected[p + 3]
    check(all_alpha0_kept, f"{name}: alpha==0 pixels preserved verbatim")
    check(alpha_never_changed, f"{name}: alpha channel never rewritten")

    # 3b. padding bytes strictly outside the pixel grid untouched.
    pad_ok = True
    for row in range(h):
        for b in range(w * 4, stride):
            i = row * stride + b
            pad_ok &= src[i] == expected[i]
    check(pad_ok, f"{name}: row padding untouched")

    # 4. input immutability: snapshot semantics of the reference.
    before = bytes(src)
    ref.apply_v1(seed, slot, w, h, stride, src)
    check(bytes(src) == before, f"{name}: src unmodified by apply_v1")

    # 7. plan-A perturbation bound: no channel moves by more than 13.
    bound_ok = True
    for p in pixels(w, h, stride):
        for c in range(3):
            bound_ok &= abs(expected[p + c] - src[p + c]) <= 13
    check(bound_ok, f"{name}: per-channel perturbation within +/-13")

# 5. input generator independent of algorithm internals & stable.
gen_a = bytes(ref.src_bytes(64, 7))
gen_b = bytes(ref.src_bytes(64, 7))
check(gen_a == gen_b, "src_bytes deterministic for fixed salt")
check(gen_a != bytes(ref.src_bytes(64, 8)), "src_bytes varies with salt")

# 6. v4 must contain preserved-transparent AND altered-opaque pixels,
# otherwise vectors 2-4 above are vacuous on this data.
name, seed, slot, w, h, stride, src, expected = next(
    c for c in cases if c[0] == "v4_4x1_alpha")
trans = [p for p in pixels(w, h, stride) if src[p + 3] == 0]
altered = [p for p in pixels(w, h, stride)
           if src[p + 3] != 0 and src[p:p + 3] != expected[p:p + 3]]
check(len(trans) >= 1 and len(altered) >= 1,
      "v4 has both preserved and altered pixels (checks non-vacuous)")

print()
print("NOTE: reference-only contract check. NOT C++ parity, NOT browser "
      "acceptance. Frozen golden fixture and parity script are still "
      "missing from the tree (see docs/canvas-implementation-review.md).")
sys.exit(1 if FAILURES else 0)
