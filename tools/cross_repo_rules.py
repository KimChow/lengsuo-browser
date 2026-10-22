"""Cross-repo check: the profile sync rules layer must match byte for byte.

Why this exists
---------------
`docs/kernel-wire-contract.md` section 4.6 pins the collection rules
layer: the `profile_sync` section name, the 32 MiB package ceiling, the
two patterns that never leave the machine, and the glob fixed vectors
that pin the matching semantics.

The kernel side implements them in `native/profile_bundle.{h,cc}` and
the client side in `crates/.../kernel_pipe/profile_rules.rs`. Each side
hardcoded its own copy of every one of those values, and each side then
asserted its own output against its own copy. That proves nothing about
the two repos agreeing.

This is the same trap task R fell into with the ConfigBundle payload, the
machine code and the frame layer: two copies of one constant, each
checked only against its own output, with nothing comparing the copies.
Change the floor pattern on one side and both sides stay green while the
two sides no longer enforce the same floor.

This layer is the privacy floor. "Cookies and Network Cache never leave
the machine" is a promise the two repos make *separately*; if their two
copies of the floor patterns differ, that promise is no longer one
promise.

So the contract is the third side of the comparison, exactly as it is
for the envelope vectors and the frame layer: a value that moves in one
repo is caught even when the other two still agree with each other. A
side that cannot be read is reported, never skipped -- an absent check
looks exactly like a passing one.

What it compares
----------------
* the `profile_sync` section name        (contract section 4.6.2)
* the package ceiling in bytes           (contract section 4.6.2)
* the two never-export patterns, in order (contract section 4.6.2)
* the glob fixed vectors                 (contract section 4.6.3)

Usage
-----
    from tools.cross_repo_rules import compare_rules

    for check in compare_rules(kernel_repo, client_repo):
        print(check.label, check.ok, check.values)

This module is read-only: it never writes to either repo.
"""
import importlib.util
import pathlib
import re
import sys

# Where each side keeps the rules layer.
CLIENT_RULES_REL = "crates/lengsuo-desktop/src/kernel_pipe/profile_rules.rs"
KERNEL_HEADER_REL = "native/profile_bundle.h"
KERNEL_SOURCE_REL = "native/profile_bundle.cc"
KERNEL_TEST_REL = "native/profile_bundle_test.cc"

# The contract's headings. A heading that moved is a section that cannot
# be read, and a section that cannot be read is an error -- never a
# silently skipped check.
CONTRACT_CONSTANTS_HEADING = "### 4.6.2 固定常量"
CONTRACT_VECTORS_HEADING = "### 4.6.3 glob 固定向量"

# The marker that identifies the envelope tool, which owns the lookup
# for where the contract is kept. The repository keeps `docs/` local
# only, so on the build machine the contract lives outside the
# repository, and that lookup must have exactly one home: the two
# checks could otherwise read different documents.
ENVELOPE_TOOL = "cross_repo_vectors"


def _sibling(name):
    """Load a module that lives next to this file.

    `tools/` is not a package and this file is run as a script as well
    as imported by path from the tests, so the sibling is loaded by path
    rather than by name.
    """
    path = pathlib.Path(__file__).resolve().parent / (name + ".py")
    spec = importlib.util.spec_from_file_location(name, path)
    module = importlib.util.module_from_spec(spec)
    spec.loader.exec_module(module)
    return module


def _envelope_tool():
    """The envelope tool, loaded once.

    It owns the contract lookup. The module is found by path rather than
    by name because `tools/` is not a package and this file is loaded by
    path from the tests. An already-loaded copy is preferred, so running
    the three checks together does not execute the envelope tool twice.
    """
    for module in (sys.modules.get("__main__"),
                   sys.modules.get(ENVELOPE_TOOL)):
        # The marker is what identifies the envelope tool. Matching on a
        # function name instead would find this module when it is run as
        # a script, and calling into it would recurse forever.
        if module is not None and getattr(module, "TOOL_NAME", None) == (
                ENVELOPE_TOOL):
            return module
    return _sibling(ENVELOPE_TOOL)


def contract_path(kernel):
    """Return the contract file, wherever this deployment keeps it."""
    return _envelope_tool().contract_path(kernel)


class RuleVectorError(Exception):
    """The rules layer's constants could not be read at all."""


class RuleCheck:
    """One comparison: the three sides' values and whether they agree."""

    def __init__(self, label, values):
        self.label = label
        # Keys are "contract", "kernel" and "client".
        self.values = values

    @property
    def ok(self):
        """True when every side is present and they all agree."""
        if len(self.values) != 3 or any(v is None for v in self.values.values()):
            return False
        first = next(iter(self.values.values()))
        return all(v == first for v in self.values.values())

    def __repr__(self):
        return "RuleCheck(%r, ok=%r)" % (self.label, self.ok)


# ---------------------------------------------------------------------------
# Small readers
# ---------------------------------------------------------------------------

def _section(text, heading, what):
    """Return the text under `heading`, up to the next heading line."""
    index = text.find(heading)
    if index < 0:
        raise RuleVectorError("the contract lost its %s section" % what)
    kept = []
    for line in text[index + len(heading):].splitlines():
        if line.startswith("#"):
            break
        kept.append(line)
    return "\n".join(kept)


def _int_expr(text):
    """Evaluate a constant expression made of integers joined by `*`.

    Both sides spell the ceiling as `32 * 1024 * 1024` (the kernel adds
    a `ULL` suffix per factor). The suffix is dropped here so the two
    spellings compare equal, because what the two repos must agree on
    is the *value*, not how the value is written.
    """
    parts = [p.strip() for p in text.split("*")]
    value = 1
    for part in parts:
        part = re.sub(r"[uUlL]+$", "", part).strip()
        if not re.fullmatch(r"\d+", part):
            raise RuleVectorError("not an integer expression: %r" % text)
        value *= int(part)
    return value


def _cpp_string(text, name):
    """Return the value of a C string constant, by name."""
    match = re.search(
        r"\b%s\s*\[\s*\]\s*=\s*\"((?:[^\"\\]|\\.)*)\"" % re.escape(name),
        text)
    if not match:
        raise RuleVectorError("the kernel lost %s" % name)
    return match.group(1)


def _rust_string(text, name):
    """Return the value of a Rust string constant, by name."""
    match = re.search(
        r"\b%s\s*:\s*&str\s*=\s*\"((?:[^\"\\]|\\.)*)\"" % re.escape(name),
        text)
    if not match:
        raise RuleVectorError("the client lost %s" % name)
    return match.group(1)


# ---------------------------------------------------------------------------
# The contract
# ---------------------------------------------------------------------------

def contract_rule_values(text):
    """Return the contract's four rule constants, keyed by role.

    The table has one row per constant. The row's own label says what
    the constant is, the value column says what it must be, and the
    last two columns name the two sides' spellings. This returns the
    value for each role so the three sides can be compared by role
    rather than by each side's own naming.
    """
    section = _section(text, CONTRACT_CONSTANTS_HEADING, "rule constants")
    # The first column is prose (it says which constant the row is),
    # sometimes with a backticked name in it; the other three columns
    # are the value and the two sides' spellings.
    rows = re.findall(
        r"^\|\s*([^|]*?)\s*\|\s*`([^`]+)`\s*\|\s*`([^`]+)`\s*\|"
        r"\s*`([^`]+)`\s*\|", section, re.M)
    if not rows:
        raise RuleVectorError("the contract lost its rule constant table")
    values = {}
    for label, value, _kernel_name, _client_name in rows:
        if "段名" in label:
            values["section key"] = value
        elif "上限" in label:
            values["max package bytes"] = value
        elif "地板模式 1" in label:
            values["floor pattern 1"] = value
        elif "地板模式 2" in label:
            values["floor pattern 2"] = value
    missing = {"section key", "max package bytes",
               "floor pattern 1", "floor pattern 2"} - set(values)
    if missing:
        raise RuleVectorError(
            "the contract's rule constant table is missing %s"
            % ", ".join(sorted(missing)))
    return values


def contract_glob_vectors(text):
    """Return the contract's glob vectors, keyed by (pattern, text).

    The conclusion column says "匹配" (match) or "不匹配" (no match).
    The empty pattern and the empty text are written as "`` （空模式）"
    and "`` （空文本）" so the table stays readable; the value is the
    empty string in both cases.
    """
    section = _section(text, CONTRACT_VECTORS_HEADING, "glob vector")
    vectors = {}
    for line in section.splitlines():
        if not line.startswith("|"):
            continue
        cells = [cell.strip() for cell in line.strip("|").split("|")]
        if len(cells) != 4 or not re.fullmatch(r"`G\d+`", cells[0]):
            continue
        pattern = _backticked(cells[1])
        subject = _backticked(cells[2])
        verdict = cells[3].replace("*", "").strip()
        if pattern is None or subject is None:
            raise RuleVectorError(
                "the contract's glob vector %s is not readable" % cells[0])
        vectors[(pattern, subject)] = verdict == "匹配"
    if not vectors:
        raise RuleVectorError("the contract lost its glob vector table")
    return vectors


def _backticked(cell):
    """Return the code inside a table cell's backticks, or None.

    The empty pattern and the empty text are written as "`` （空模式）"
    so the table stays readable; the value is the empty string in both
    cases, and the prose that follows the backticks is not part of it.
    """
    match = re.search(r"`([^`]*)`", cell)
    if not match:
        return None
    return match.group(1)


# ---------------------------------------------------------------------------
# The kernel side
# ---------------------------------------------------------------------------

def kernel_rule_constants(header, source):
    """Return the kernel's four rule constants, keyed by role."""
    key = _cpp_string(header, "kProfileSyncConfigKey")
    ceiling = re.search(
        r"kProfileBundleMaxBytes\s*=\s*([^;]+);", header)
    if not ceiling:
        raise RuleVectorError("the kernel lost kProfileBundleMaxBytes")
    return {
        "section key": key,
        "max package bytes": _int_expr(ceiling.group(1)),
        "floor pattern 1": _cpp_string(source, "kForbiddenCookies"),
        "floor pattern 2": _cpp_string(source, "kForbiddenNetwork"),
    }


def kernel_glob_vectors(text):
    """Return the kernel's glob assertions, keyed by (pattern, text).

    The kernel writes them as `EXPECT_TRUE(MatchGlob("p", "t"))` and
    `EXPECT_FALSE(MatchGlob("p", "t"))`. Only the assertions that carry
    two string literals count: a call with a computed argument is not a
    fixed vector.
    """
    vectors = {}
    for verdict, pattern, subject in re.findall(
            r"EXPECT_(TRUE|FALSE)\s*\(\s*MatchGlob\s*\(\s*"
            r"\"((?:[^\"\\]|\\.)*)\"\s*,\s*\"((?:[^\"\\]|\\.)*)\"\s*\)",
            text):
        vectors[(pattern, subject)] = verdict == "TRUE"
    if not vectors:
        raise RuleVectorError("the kernel lost its glob assertions")
    return vectors


# ---------------------------------------------------------------------------
# The client side
# ---------------------------------------------------------------------------

def client_rule_constants(text):
    """Return the client's four rule constants, keyed by role."""
    key = _rust_string(text, "PROFILE_SYNC_KEY")
    ceiling = re.search(
        r"MAX_PACKAGE_BYTES\s*:\s*u64\s*=\s*([^;]+);", text)
    if not ceiling:
        raise RuleVectorError("the client lost MAX_PACKAGE_BYTES")
    forbidden = re.search(
        r"FORBIDDEN\s*:\s*\[\s*&str\s*;\s*2\s*\]\s*=\s*\[([^\]]*)\]",
        text)
    if not forbidden:
        raise RuleVectorError("the client lost FORBIDDEN")
    patterns = re.findall(r"\"((?:[^\"\\]|\\.)*)\"", forbidden.group(1))
    if len(patterns) != 2:
        raise RuleVectorError(
            "the client's FORBIDDEN does not hold exactly two patterns")
    return {
        "section key": key,
        "max package bytes": _int_expr(ceiling.group(1)),
        "floor pattern 1": patterns[0],
        "floor pattern 2": patterns[1],
    }


def client_glob_vectors(text):
    """Return the client's glob assertions, keyed by (pattern, text).

    The client writes them as `assert!(match_glob("p", "t"))` and
    `assert!(!match_glob("p", "t"))`. The negative form is written
    across two lines in places, so the pattern allows whitespace inside
    the macro call.
    """
    vectors = {}
    for negated, pattern, subject in re.findall(
            r"assert!\s*\(\s*(!?)\s*match_glob\s*\(\s*"
            r"\"((?:[^\"\\]|\\.)*)\"\s*,\s*\"((?:[^\"\\]|\\.)*)\"\s*\)",
            text):
        vectors[(pattern, subject)] = negated != "!"
    if not vectors:
        raise RuleVectorError("the client lost its glob assertions")
    return vectors


# ---------------------------------------------------------------------------
# The comparison
# ---------------------------------------------------------------------------

def compare_rules(kernel_root, client_root):
    """Compare the three sides' rules layer.

    Returns a list of :class:`RuleCheck`. A side that cannot be read
    raises :class:`RuleVectorError` rather than producing a check that
    quietly omits it -- an absent check looks exactly like a passing one.
    """
    kernel_root = pathlib.Path(kernel_root)
    client_root = pathlib.Path(client_root)

    def read(root, relative):
        path = root / relative
        if not path.is_file():
            raise RuleVectorError("cannot read %s" % path)
        return path.read_text(encoding="utf-8")

    contract_text = contract_path(kernel_root).read_text(encoding="utf-8")
    kernel_header = read(kernel_root, KERNEL_HEADER_REL)
    kernel_source = read(kernel_root, KERNEL_SOURCE_REL)
    kernel_test = read(kernel_root, KERNEL_TEST_REL)
    client_rules = read(client_root, CLIENT_RULES_REL)

    contract_constants = contract_rule_values(contract_text)
    kernel_constants = kernel_rule_constants(kernel_header, kernel_source)
    client_constants = client_rule_constants(client_rules)

    # The ceiling is a number on both sides and a number in the
    # contract; the other three are strings. The contract's ceiling is
    # written in decimal, so it is read as a number too rather than
    # compared as text.
    def ceiling(value):
        return _int_expr(value) if isinstance(value, str) else value

    checks = []
    for role in ("section key", "max package bytes",
                 "floor pattern 1", "floor pattern 2"):
        value = contract_constants[role]
        if role == "max package bytes":
            value = ceiling(value)
        checks.append(RuleCheck(role, {
            "contract": value,
            "kernel": kernel_constants[role],
            "client": client_constants[role],
        }))

    checks.append(RuleCheck("glob vectors", {
        "contract": contract_glob_vectors(contract_text),
        "kernel": kernel_glob_vectors(kernel_test),
        "client": client_glob_vectors(client_rules),
    }))
    return checks


def main(argv=None):
    """Run the check and report each comparison."""
    import argparse
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("--kernel", required=True,
                        help="path to the kernel repo working copy")
    parser.add_argument("--client", required=True,
                        help="path to the client repo working copy")
    args = parser.parse_args(argv)

    try:
        checks = compare_rules(args.kernel, args.client)
    except (OSError, RuleVectorError) as error:
        print("rules layer            %s" % error, file=sys.stderr)
        return 2

    ok = True
    for check in checks:
        print("%-22s %-18s %s" % (check.label, "",
                                  "OK" if check.ok else "MISMATCH"))
        if not check.ok:
            ok = False
            for name, value in check.values.items():
                print("    %-9s %s" % (name, value))
    return 0 if ok else 1


if __name__ == "__main__":
    sys.exit(main())
