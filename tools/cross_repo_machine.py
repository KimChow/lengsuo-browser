"""Cross-repo check: the machine-fact collection rules must match.

Why this exists
---------------
The wire contract fixes the *shape* of the machine code - four fields, one
key order, one derivation - but not what each field holds. Task M0 fixes
that: the kernel and the client must collect the *same four values* on the
same machine, or the two sides derive different machine keys and the
handshake fails closed.

Before this card the two repos collected different values on the very same
machine (measured on the research machine: `os` was "Ubuntu 24.04.1 LTS"
on the kernel side and "linux" on the client side, `hddid` was the device
number on one side and the disk serial on the other), so the two machine
codes could not have agreed.

Each side asserting its own output against its own expectation proves
nothing about the two repos agreeing - that is the trap task R fell into
one layer down, and the trap the profile rules layer fell into. So the two
sides publish the same *rule vectors*: a fixed set of machines, expressed
as the four facts plus the two digests derived from them, with fake values
so no real machine identifier is committed.

This tool is the comparison. It reads both sides' tables, compares them
field by field, and recomputes the digests from the four facts with its
own implementation of the contract's derivation - so a side that publishes
a digest that does not follow from its own fields is caught, rather than
being taken on trust.

A side that has not published its table yet is reported as PENDING, which
is neither OK nor a mismatch: the client-side card
(`m0-machine-code-align`) is a separate lane and may not have landed.
PENDING is deliberately not OK - an absent check looks exactly like a
passing one.

Usage
-----
    from tools.cross_repo_machine import compare_machine

    for check in compare_machine(kernel_repo, client_repo):
        print(check.label, check.ok, check.pending, check.values)

    python3 tools/cross_repo_machine.py --kernel . --client <client repo>

This module is read-only: it never writes to either repo.
"""
import hashlib
import pathlib
import re
import sys

# Identifies this module to the frame-layer check, the same way the other
# cross-repo tools identify themselves.
TOOL_NAME = "cross_repo_machine"

# Where each side keeps its rule vectors.
#
# The kernel's table is the one in the gtest: the vectors are assertions
# there, so the test file is where they live.
KERNEL_VECTOR_REL = "native/machine_facts_test.cc"

# The client side's table. The client task book names
# `kernel_pipe/tests.rs`, and the two files next to it are tried as well,
# because the card is allowed to split the machine module into its own
# file. The first file that carries the table wins.
CLIENT_VECTOR_CANDIDATES = (
    "crates/lengsuo-desktop/src/kernel_pipe/tests.rs",
    "crates/lengsuo-desktop/src/kernel_pipe/machine_vectors.rs",
    "crates/lengsuo-desktop/src/kernel_pipe/machine_tests.rs",
    "crates/lengsuo-desktop/src/kernel_pipe/machine.rs",
)

# The seven values a rule vector carries. The order is the contract's key
# order for the four facts, then the two digests.
FIELDS = (
    "label",
    "os",
    "cpuid",
    "hddid",
    "computername",
    "machine_code",
    "k_machine",
)

# The four facts, in the contract's key order. The JSON the contract
# derives the machine code from is these four values, in this order, with
# no other key.
FACT_FIELDS = ("os", "cpuid", "hddid", "computername")

# A machine code is 64 hex characters, lower case.
_HEX64 = re.compile(r"\A[0-9a-f]{64}\Z")


class MachineVectorError(Exception):
    """The rule vectors could not be read at all."""


class MachineCheck:
    """One comparison: the two sides' values, and whether they agree."""

    def __init__(self, label, values, pending=False):
        self.label = label
        # Keys are "kernel" and "client".
        self.values = values
        # True when a side has not published its table yet. A pending
        # check is not a passing check.
        self.pending = pending

    @property
    def ok(self):
        """True when every side that should be there is there, and they agree.

        A check with one side is a check about that side alone (the digest
        recomputation, which each side can fail on its own); its value is
        the list of rows that do not recompute, so "ok" means "empty". A
        check with two sides needs both: a missing side is a failure, not
        a pass.
        """
        if self.pending:
            return False
        if not self.values or any(v is None for v in self.values.values()):
            return False
        if len(self.values) == 1:
            only = next(iter(self.values.values()))
            return isinstance(only, list) and not only
        first = next(iter(self.values.values()))
        return all(v == first for v in self.values.values())

    def __repr__(self):
        return "MachineCheck(%r, ok=%r, pending=%r)" % (
            self.label, self.ok, self.pending)


# ---------------------------------------------------------------------------
# The derivation, recomputed here
# ---------------------------------------------------------------------------

def machine_json(facts):
    """The JSON the contract derives the machine code from.

    The contract's shape: the four facts, in the key order os, cpuid,
    hddid, computername, as JSON strings, with no other key and no
    whitespace. The escaping is the JSON one, and it matches the kernel's
    `MachineCodeJson` byte for byte.
    """
    out = "{"
    for index, name in enumerate(FACT_FIELDS):
        if index:
            out += ","
        out += '"%s":"%s"' % (name, _json_escape(facts[name]))
    out += "}"
    return out


def _json_escape(text):
    """Escape a value the way the kernel's JSON writer does."""
    out = []
    for char in text:
        code = ord(char)
        if char == '"':
            out.append('\\"')
        elif char == "\\":
            out.append("\\\\")
        elif char == "\b":
            out.append("\\b")
        elif char == "\f":
            out.append("\\f")
        elif char == "\n":
            out.append("\\n")
        elif char == "\r":
            out.append("\\r")
        elif char == "\t":
            out.append("\\t")
        elif code < 0x20:
            out.append("\\u%04x" % code)
        else:
            out.append(char)
    return "".join(out)


def derive_machine_code(facts):
    """machine_code = SHA-256(UTF-16LE(the JSON)), lower case hex."""
    return hashlib.sha256(
        machine_json(facts).encode("utf-16-le")).hexdigest()


def derive_k_machine(machine_code):
    """k_machine = SHA-256(machine_code's ASCII bytes), lower case hex."""
    return hashlib.sha256(machine_code.encode("ascii")).hexdigest()


# ---------------------------------------------------------------------------
# Reading the two sides
# ---------------------------------------------------------------------------

def _cpp_strings(block):
    """Return the string literals of a block, in order."""
    return re.findall(r'"((?:[^"\\]|\\.)*)"', block)


def _unquote(text):
    """Undo the C/Rust string escaping of a plain literal."""
    return (text.replace("\\\\", "\x00")
                .replace('\\"', '"')
                .replace("\\n", "\n")
                .replace("\\r", "\r")
                .replace("\\t", "\t")
                .replace("\x00", "\\"))


def kernel_rule_vectors(kernel_root):
    """Read the kernel side's rule vectors.

    The table is an array of brace-enclosed rows of string literals, in
    the order the fields are listed in `FIELDS`.
    """
    path = pathlib.Path(kernel_root) / KERNEL_VECTOR_REL
    if not path.is_file():
        raise MachineVectorError("cannot read %s" % path)
    text = path.read_text(encoding="utf-8")
    start = text.find("kRuleVectors")
    if start < 0:
        raise MachineVectorError(
            "the kernel test lost its kRuleVectors table")
    open_brace = text.find("{", start)
    if open_brace < 0:
        raise MachineVectorError("the kernel's kRuleVectors table is broken")
    depth = 0
    end = open_brace
    for index in range(open_brace, len(text)):
        if text[index] == "{":
            depth += 1
        elif text[index] == "}":
            depth -= 1
            if depth == 0:
                end = index
                break
    else:
        raise MachineVectorError("the kernel's kRuleVectors table is broken")

    rows = []
    for row in re.finditer(r"\{([^{}]*)\}", text[open_brace:end + 1]):
        values = _cpp_strings(row.group(1))
        if len(values) != len(FIELDS):
            raise MachineVectorError(
                "a kernel rule vector has %d values, not %d"
                % (len(values), len(FIELDS)))
        rows.append(dict(zip(FIELDS, [_unquote(v) for v in values])))
    if not rows:
        raise MachineVectorError(
            "the kernel's kRuleVectors table carries no row")
    return rows


def _rust_blocks(text):
    """Yield the brace-enclosed blocks of a Rust source, outermost first.

    A block is a `{ ... }` pair with no nested braces, so the fields of a
    struct literal are read without having to know the struct's name.
    """
    for match in re.finditer(r"\{([^{}]*)\}", text):
        yield match.group(1)


def client_rule_vectors(client_root):
    """Read the client side's rule vectors, or None if it has none yet.

    The client publishes the rows as struct literals with the seven field
    names. The container's name is not pinned - the card is free to call it
    whatever - but the field names are, because they are what the two
    sides have in common.

    None means "not published yet", which is not the same as "empty" and
    not the same as "disagrees".
    """
    client_root = pathlib.Path(client_root)
    for relative in CLIENT_VECTOR_CANDIDATES:
        path = client_root / relative
        if not path.is_file():
            continue
        text = path.read_text(encoding="utf-8")
        rows = []
        for block in _rust_blocks(text):
            fields = dict(re.findall(
                r'\b([A-Za-z_][A-Za-z0-9_]*)\s*:\s*"((?:[^"\\]|\\.)*)"',
                block))
            if not all(name in fields for name in FIELDS):
                continue
            rows.append({name: _unquote(fields[name]) for name in FIELDS})
        if rows:
            return rows
    return None


# ---------------------------------------------------------------------------
# The comparison
# ---------------------------------------------------------------------------

def compare_machine(kernel_root, client_root):
    """Compare the two sides' rule vectors.

    Returns a list of :class:`MachineCheck`. The kernel side is required:
    a kernel table that cannot be read raises :class:`MachineVectorError`.
    The client side may be absent, in which case its checks come back
    pending rather than absent.
    """
    kernel_rows = kernel_rule_vectors(kernel_root)
    client_rows = client_rule_vectors(client_root)
    client_pending = client_rows is None

    checks = []
    kernel_by_label = {row["label"]: row for row in kernel_rows}
    client_by_label = ({row["label"]: row for row in client_rows}
                        if client_rows else {})

    labels = sorted(set(kernel_by_label) | set(client_by_label))
    for label in labels:
        checks.append(MachineCheck(
            "rule vector %s" % label,
            {
                "kernel": kernel_by_label.get(label),
                "client": client_by_label.get(label),
            },
            pending=client_pending))

    # The two sides must test the same set of machines, not just agree on
    # the ones they share. A side that quietly dropped a row would still
    # pass every per-row check.
    checks.append(MachineCheck(
        "rule vector set",
        {
            "kernel": sorted(kernel_by_label),
            "client": sorted(client_by_label) if client_rows else None,
        },
        pending=client_pending))

    # The digests must follow from the four facts. This is the check that
    # catches a side that published a table whose digests were edited by
    # hand rather than computed: the per-row check compares the two sides
    # to each other, this one checks each side against itself.
    #
    # The value is the list of rows whose digests do not recompute, so an
    # empty list means every row recomputed.
    for side, rows in (("kernel", kernel_rows), ("client", client_rows)):
        if rows is None:
            continue
        broken = []
        for row in rows:
            facts = {name: row[name] for name in FACT_FIELDS}
            if derive_machine_code(facts) != row["machine_code"]:
                broken.append(row["label"])
            elif derive_k_machine(row["machine_code"]) != row["k_machine"]:
                broken.append(row["label"])
        checks.append(MachineCheck(
            "%s digests recompute" % side,
            {side: broken},
            pending=False))
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
        checks = compare_machine(args.kernel, args.client)
    except (OSError, MachineVectorError) as error:
        print("machine rules        %s" % error, file=sys.stderr)
        return 2

    ok = True
    pending = False
    for check in checks:
        if check.pending:
            state = "PENDING"
            pending = True
            detail = ""
        elif check.ok:
            state = "OK"
            detail = ""
        else:
            state = "MISMATCH"
            ok = False
            detail = check.values
        print("%-28s %-9s %s" % (check.label, state, detail))
    if pending:
        print("the client side has not published its rule vectors yet; "
              "the comparison above is not a pass")
    return 0 if ok and not pending else 1


if __name__ == "__main__":
    sys.exit(main())
