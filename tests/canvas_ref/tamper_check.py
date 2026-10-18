"""Tamper gate for the frozen canvas_noise v1 golden fixture.

Positive path: parse tests/canvas_ref/golden_fixture.inc, recompute every
vector with tests/canvas_ref/gen_canvas_ref.py, and compare fields and
bytes exactly.

Negative control (the reason this script exists): rebuild the reference
module in memory with a hash constant (CONST_A) swapped to a different
value, recompute, and REQUIRE that the tampered output DIFFERS from the
frozen golden. If a tampered generator still reproduced the golden bytes,
the fixture would carry no signal and this gate would be toothless, so
that case is a hard failure here. A second negative control flips one
golden byte in memory and requires the comparator to notice.

Scope guard: this freezes PYTHON REFERENCE bytes only. It is not C++
parity, not browser acceptance, not anti-fingerprinting evidence; the
C++ batch and native/canvas_noise_golden.inc are separate deliverables
(see docs/canvas-golden-freeze.md).

Exits nonzero if any assertion fails.
"""
import copy
import os
import re
import sys
import types

HERE = os.path.dirname(os.path.abspath(__file__))
REF_PATH = os.path.join(HERE, "gen_canvas_ref.py")
GOLDEN_PATH = os.path.join(HERE, "golden_fixture.inc")

# Frozen fixture grammar (emitted by gen_canvas_ref.fixture_text + header).
CASE_RE = re.compile(
    r'kCase_(\w+) = \{\s*"(\w+)", (\d+)ULL, (\d+)U, (\d+)U, (\d+)U, '
    r'(\d+)ULL, (\d+)U,')
ARR_RE = re.compile(r'k(Src|Exp)_(\w+)\[\] = \{([^}]*)\};')

CONST_A_LINE = "CONST_A = 0x9E3779B97F4A7C15"
CONST_A_TAMPERED = "CONST_A = 0x9E3779B97F4A7C16"

FAILURES = []


def check(cond, label):
    print(("PASS " if cond else "FAIL ") + label)
    if not cond:
        FAILURES.append(label)


def parse_byte_array(body):
    toks = (t for t in body.replace("\n", " ").split(",") if t.strip())
    return bytes(int(t) for t in toks)


def parse_fixture(text):
    """Return {name: {seed,slot,width,height,stride,size,src,expected}}."""
    cases = {}
    for m in CASE_RE.finditer(text):
        cases[m.group(1)] = {
            "seed": int(m.group(3)), "slot": int(m.group(4)),
            "width": int(m.group(5)), "height": int(m.group(6)),
            "stride": int(m.group(7)), "size": int(m.group(8)),
        }
    for m in ARR_RE.finditer(text):
        kind, name = m.group(1), m.group(2)
        if name not in cases:
            raise AssertionError(f"array k{kind}_{name} has no kCase record")
        cases[name]["src" if kind == "Src" else "expected"] = \
            parse_byte_array(m.group(3))
    for name, c in cases.items():
        for field in ("src", "expected"):
            if field not in c:
                raise AssertionError(f"{name}: missing k{field.capitalize()} array")
            if len(c[field]) != c["size"]:
                raise AssertionError(
                    f"{name}: {field} has {len(c[field])} bytes, size says {c['size']}")
    return cases


def load_ref(source_text, module_name):
    mod = types.ModuleType(module_name)
    mod.__file__ = REF_PATH
    exec(compile(source_text, REF_PATH, "exec"), mod.__dict__)
    return mod


def compare(frozen, recomputed):
    """Return list of mismatch strings between fixture and generator output.

    Empty list means byte-for-byte agreement on metadata, src, and expected.
    """
    problems = []
    names = [name for name, *_ in recomputed]
    if sorted(frozen) != sorted(names):
        problems.append(
            f"vector sets differ: fixture={sorted(frozen)} gen={sorted(names)}")
    for name, seed, slot, w, h, stride, src, expected in recomputed:
        if name not in frozen:
            problems.append(f"{name}: absent from fixture")
            continue
        c = frozen[name]
        meta = (c["seed"], c["slot"], c["width"], c["height"],
                c["stride"], c["size"])
        want = (seed, slot, w, h, stride, len(src))
        if meta != want:
            problems.append(f"{name}: metadata {meta} != frozen {want}")
        if c["src"] != src:
            problems.append(f"{name}: src bytes differ from fixture")
        if c["expected"] != expected:
            problems.append(f"{name}: expected bytes differ from fixture")
    return problems


def first_diff(a, b):
    for i, (x, y) in enumerate(zip(a, b)):
        if x != y:
            return i, x, y
    if len(a) != len(b):
        return min(len(a), len(b)), None, None
    return None


def main():
    with open(REF_PATH, encoding="utf-8") as f:
        ref_src = f.read()
    with open(GOLDEN_PATH, encoding="utf-8") as f:
        golden_text = f.read()

    frozen = parse_fixture(golden_text)
    check(len(frozen) == 5,
          f"fixture parses as exactly 5 vectors (got {len(frozen)})")

    # ---- positive path: clean reference must reproduce the frozen bytes ----
    clean = load_ref(ref_src, "canvas_ref_clean")
    problems = compare(frozen, list(clean.build_cases()))
    check(not problems,
          "clean reference reproduces golden bytes exactly"
          + ("" if not problems else "; " + "; ".join(problems)))

    # ---- negative control 1: tamper the hash constant, require failure ----
    check(ref_src.count(CONST_A_LINE) == 1,
          "negative control preflight: CONST_A line located once in source")
    tampered_src = ref_src.replace(CONST_A_LINE, CONST_A_TAMPERED)
    check(tampered_src != ref_src,
          "negative control preflight: CONST_A substitution applied")
    tampered = load_ref(tampered_src, "canvas_ref_tampered")
    t_cases = list(tampered.build_cases())
    src_same = all(frozen[n]["src"] == s
                   for n, _, _, _, _, _, s, _ in t_cases)
    check(src_same,
          "negative control: tampered input generator still yields same src "
          "(only the hash stream changed)")
    changed = [n for n, seed, slot, w, h, stride, src, expected in t_cases
               if frozen[n]["expected"] != expected]
    check(len(changed) >= 1,
          "negative control bites: CONST_A tampering DOES break byte-for-byte "
          f"comparison (vectors altered: {sorted(changed) or 'NONE -- gate is toothless!'})")
    for name, seed, slot, w, h, stride, src, expected in t_cases:
        if name in changed:
            i, x, y = first_diff(frozen[name]["expected"], expected)
            print(f"      first expected-byte diff in {name}: offset {i}, "
                  f"frozen={x}, tampered={y}")

    # ---- negative control 2: flip one frozen byte, comparator must notice --
    corrupted = copy.deepcopy(frozen)
    target = bytearray(corrupted["v5_16x16"]["expected"])
    target[0] ^= 0x01
    corrupted["v5_16x16"]["expected"] = bytes(target)
    problems2 = compare(corrupted, list(clean.build_cases()))
    check(any("v5_16x16" in p and "expected" in p for p in problems2),
          "negative control bites: single-byte golden corruption is caught "
          f"by byte-for-byte compare ({len(problems2)} mismatch(es) reported)")

    print()
    print("NOTE: golden covers the PYTHON reference bytes only. This is NOT "
          "C++ parity (native/canvas_noise_golden.inc: not delivered), NOT "
          "browser acceptance, NOT anti-fingerprinting evidence.")
    sys.exit(1 if FAILURES else 0)


if __name__ == "__main__":
    main()
