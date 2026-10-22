"""Cross-repo check: the *bootstrap* layer must match byte for byte.

Why this exists
---------------
`docs/kernel-wire-contract.md` section 1.1 pins the constants the two
sides need **before a single frame byte is on the wire**: the endpoint
prefix, the command-line switch name, the HKDF info string, four byte
counts, the two OS endpoint shapes, and the Hello JSON key names.

The kernel declares them in `integration/kernel_pipe_client.{h,cc}`,
`native/kernel_pipe_keys.h`, `native/kernel_pipe_envelope.h` and
`native/kernel_pipe.cc`; the client in
`crates/.../kernel_pipe/{launch,keys,handshake}.rs`. Each side wrote its
own copy, and each side then asserted its own output against its own
copy -- which proves nothing about the two repos agreeing. That is the
trap task R fell into with the ConfigBundle payload, already closed one
layer at a time for the envelope bytes, the machine code, the frame
layer and the profile rules layer.

Why this layer is worth its own guard
-------------------------------------
These values fail earlier and in a more misleading way than any other
layer. Change the endpoint prefix (or the switch name) on one side and
the kernel dials a different endpoint, so the runner's accept() times
out: neither side reports a protocol error, both report "cannot
connect". Change the HKDF info string and the envelope fails to
authenticate, so the kernel aborts as fail-closed. In every case the two
repos' own test suites stay green, because each one still agrees with
itself.

What it compares
----------------
Ten values, three sides (contract, kernel, client), keyed by role so the
two repos' own naming does not matter:

* endpoint prefix                       (contract section 1.1)
* pipe switch name                      (contract section 1.1)
* HKDF info string                      (contract section 1.1)
* session key bytes                     (contract section 1.1)
* machine key bytes                     (contract section 1.1)
* Hello nonce bytes                     (contract section 1.1)
* GCM nonce bytes                       (contract section 1.1)
* Windows pipe root                     (contract section 1.1)
* POSIX abstract-namespace marker       (contract section 1.1)
* Hello JSON key names                  (contract section 1.1)

Two of these are written in different spellings on the two sides and are
compared by value, not by how they are written:

* the switch name -- the kernel holds the bare name it hands to
  base::CommandLine, the client holds the `--name=` prefix it appends;
* the Windows pipe root -- a C literal doubles every backslash, a Rust
  raw string does not, and the contract writes it literally.

A side that cannot be read is reported, never skipped: an absent check
looks exactly like a passing one.

Usage
-----
    from tools.cross_repo_bootstrap import compare_bootstrap

    for check in compare_bootstrap(kernel_repo, client_repo):
        print(check.label, check.ok, check.values)

This module is read-only: it never writes to either repo.
"""
import importlib.util
import pathlib
import re
import sys

# Where each side keeps the bootstrap layer.
KERNEL_CLIENT_H_REL = "integration/kernel_pipe_client.h"
KERNEL_CLIENT_CC_REL = "integration/kernel_pipe_client.cc"
KERNEL_KEYS_H_REL = "native/kernel_pipe_keys.h"
KERNEL_ENVELOPE_H_REL = "native/kernel_pipe_envelope.h"
KERNEL_PIPE_CC_REL = "native/kernel_pipe.cc"

CLIENT_LAUNCH_REL = "crates/lengsuo-desktop/src/kernel_pipe/launch.rs"
CLIENT_KEYS_REL = "crates/lengsuo-desktop/src/kernel_pipe/keys.rs"
CLIENT_HANDSHAKE_REL = "crates/lengsuo-desktop/src/kernel_pipe/handshake.rs"

# The contract's heading. A heading that moved is a section that cannot
# be read, and a section that cannot be read is an error -- never a
# silently skipped check.
CONTRACT_HEADING = "### 1.1 ⭐ 启动层固定常量（两侧逐字节相同）"

# The one sentinel the contract uses for a byte markdown cannot carry.
NUL_SENTINEL = "<NUL>"

# The marker that identifies the envelope tool, which owns the lookup for
# where the contract is kept. The repository keeps `docs/` local only, so
# on the build machine the contract lives outside the repository, and that
# lookup must have exactly one home: the two checks could otherwise read
# different documents.
ENVELOPE_TOOL = "cross_repo_vectors"


def _sibling(name):
    """Load a module that lives next to this file.

    `tools/` is not a package and this file is run as a script as well as
    imported by path from the tests, so the sibling is loaded by path
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
    the whole check set together does not execute the envelope tool twice.
    """
    for module in (sys.modules.get("__main__"),
                   sys.modules.get(ENVELOPE_TOOL)):
        # The marker is what identifies the envelope tool. Matching on a
        # function name instead would find this module when it is run as a
        # script, and calling into it would recurse forever.
        if module is not None and getattr(module, "TOOL_NAME", None) == (
                ENVELOPE_TOOL):
            return module
    return _sibling(ENVELOPE_TOOL)


def contract_path(kernel):
    """Return the contract file, wherever this deployment keeps it."""
    return _envelope_tool().contract_path(kernel)


class BootstrapVectorError(Exception):
    """The bootstrap layer's constants could not be read at all."""


class BootstrapCheck:
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
        return "BootstrapCheck(%r, ok=%r)" % (self.label, self.ok)


# ---------------------------------------------------------------------------
# Small readers
# ---------------------------------------------------------------------------

def _section(text, heading, what):
    """Return the text under `heading`, up to the next heading line."""
    index = text.find(heading)
    if index < 0:
        raise BootstrapVectorError("the contract lost its %s section" % what)
    kept = []
    for line in text[index + len(heading):].splitlines():
        if line.startswith("#"):
            break
        kept.append(line)
    return "\n".join(kept)


def _unescape(literal, escapes):
    """Decode backslash escapes in a C or Rust string literal body.

    Only the escapes these constants actually use are understood, and an
    unknown one raises: silently passing an undecoded escape through
    would compare the spelling instead of the value.
    """
    out = []
    index = 0
    while index < len(literal):
        char = literal[index]
        if char != "\\":
            out.append(char)
            index += 1
            continue
        if index + 1 >= len(literal):
            raise BootstrapVectorError("a literal ends in a bare backslash")
        nxt = literal[index + 1]
        if nxt in escapes:
            out.append(escapes[nxt])
            index += 2
        elif nxt == "x":
            digits = literal[index + 2:index + 4]
            if not re.fullmatch(r"[0-9a-fA-F]{2}", digits or ""):
                raise BootstrapVectorError("a literal has a bad \\x escape")
            out.append(chr(int(digits, 16)))
            index += 4
        else:
            raise BootstrapVectorError("a literal has an unknown escape: \\%s"
                                       % nxt)
    return "".join(out)


C_ESCAPES = {"0": "\x00", "\\": "\\", '"': '"', "n": "\n", "t": "\t",
             "r": "\r", "'": "'"}
RUST_ESCAPES = {"0": "\x00", "\\": "\\", '"': '"', "n": "\n", "t": "\t",
                "r": "\r", "'": "'"}


def _strip_literals(text):
    """Blank out comments and string/char literals, preserving offsets.

    Two jobs. First, a function body is found by matching braces, and the
    braces inside a JSON payload are inside a string literal -- counting
    them would end the body at the wrong place. Second, a search for a
    definition has to skip the *call sites* that share the name: the
    client's `Endpoint::path()` calls `windows_endpoint(name)` before the
    function is defined, so searching for the bare name finds the call
    and returns the wrong body.

    The text is blanked rather than removed so every offset still refers
    to the same character in the original, which lets the callers read
    the literals back out of the original text.
    """
    out = []
    index = 0
    length = len(text)
    while index < length:
        char = text[index]
        if char == "/" and text[index + 1:index + 2] == "/":
            end = text.find("\n", index)
            end = length if end < 0 else end
            out.append(" " * (end - index))
            index = end
            continue
        if char == "/" and text[index + 1:index + 2] == "*":
            end = text.find("*/", index + 2)
            end = length if end < 0 else end + 2
            out.append("".join(c if c == "\n" else " " for c in text[index:end]))
            index = end
            continue
        if char == "r" and text[index + 1:index + 2] in ('"', "'"):
            quote = text[index + 1]
            end = text.find(quote, index + 2)
            end = length if end < 0 else end + 1
            out.append("".join(c if c == "\n" else " " for c in text[index:end]))
            index = end
            continue
        if char in ('"', "'"):
            quote = char
            end = index + 1
            while end < length:
                if text[end] == "\\":
                    end += 2
                    continue
                if text[end] == quote:
                    break
                end += 1
            end = min(end + 1, length)
            out.append("".join(c if c == "\n" else " " for c in text[index:end]))
            index = end
            continue
        out.append(char)
        index += 1
    return "".join(out)


def _function_body(text, name, rust=False):
    """Return the braced body of a function definition, by name.

    The definition is found by its declaration, not by the bare name: a
    Rust function is `fn <name>(`, and a C++ member function is
    `<Class>::<name>(`. Searching for the bare name would find a call
    site first and return the wrong body.
    """
    stripped = _strip_literals(text)
    if rust:
        pattern = r"\bfn\s+%s\s*\(" % re.escape(name)
    else:
        pattern = r"::\s*%s\s*\(" % re.escape(name)
    match = re.search(pattern, stripped)
    if not match:
        raise BootstrapVectorError("the source lost the definition of %s"
                                   % name)
    start = stripped.find("{", match.end())
    if start < 0:
        raise BootstrapVectorError("%s has no body" % name)
    depth = 0
    for position in range(start, len(stripped)):
        char = stripped[position]
        if char == "{":
            depth += 1
        elif char == "}":
            depth -= 1
            if depth == 0:
                return text[start + 1:position]
    raise BootstrapVectorError("%s has an unterminated body" % name)


def _cpp_string(text, name):
    """Return the value of a C string constant, by name.

    The declaration may wrap across lines, so the separator between the
    name and the value is whitespace in general rather than a space.
    """
    match = re.search(
        r"\b%s\s*\[\s*\]\s*=\s*\"((?:[^\"\\]|\\.)*)\"" % re.escape(name),
        text, re.S)
    if not match:
        raise BootstrapVectorError("the kernel lost %s" % name)
    return _unescape(match.group(1), C_ESCAPES)


def _cpp_size(text, name):
    """Return the value of a C integer constant, by name."""
    match = re.search(r"\b%s\s*=\s*(\d+)\s*;" % re.escape(name), text, re.S)
    if not match:
        raise BootstrapVectorError("the kernel lost %s" % name)
    return int(match.group(1))


def _rust_str(text, name):
    """Return the value of a Rust `&str` or `&[u8]` constant, by name."""
    match = re.search(
        r"\b%s\s*:\s*&(?:\[u8\]|str)\s*=\s*b?\"((?:[^\"\\]|\\.)*)\""
        % re.escape(name), text, re.S)
    if not match:
        raise BootstrapVectorError("the client lost %s" % name)
    return _unescape(match.group(1), RUST_ESCAPES)


def _rust_usize(text, name):
    """Return the value of a Rust `usize` constant, by name."""
    match = re.search(r"\b%s\s*:\s*usize\s*=\s*(\d+)\s*;" % re.escape(name),
                      text, re.S)
    if not match:
        raise BootstrapVectorError("the client lost %s" % name)
    return int(match.group(1))


def _read_rust_literal(text, index):
    """Read one Rust string literal starting at `index`, and decode it.

    A raw string (`r"..."`) is taken as written; a plain string is
    unescaped. Both spellings occur in this layer, and the contract's
    value is the bytes, not the spelling.
    """
    if text.startswith('r"', index):
        end = text.find('"', index + 2)
        if end < 0:
            raise BootstrapVectorError("a raw string is unterminated")
        return text[index + 2:end]
    if text.startswith('"', index):
        position = index + 1
        while position < len(text):
            if text[position] == "\\":
                position += 2
                continue
            if text[position] == '"':
                return _unescape(text[index + 1:position], RUST_ESCAPES)
            position += 1
        raise BootstrapVectorError("a string literal is unterminated")
    raise BootstrapVectorError("expected a string literal at offset %d" % index)


def _format_prefix(text, function_name):
    """Return the literal head of the `format!` call inside a function.

    Everything before the first `{` is the fixed part of the endpoint
    template, which is exactly the part the two sides must agree on: the
    rest is the pipe name, which is random per run by design.
    """
    body = _function_body(text, function_name, rust=True)
    match = re.search(r"format!\s*\(\s*", body)
    if not match:
        raise BootstrapVectorError("%s has no format! call" % function_name)
    literal = _read_rust_literal(body, match.end())
    cut = literal.find("{")
    return literal if cut < 0 else literal[:cut]


def _cpp_wide_prefix(text, function_name):
    """Return the wide-string literal head inside a C++ function.

    The Windows pipe root is the only wide literal in `OpenEndpoint`, and
    the pipe name is appended to it rather than embedded, so the literal
    is the whole fixed part.
    """
    body = _function_body(text, function_name)
    win = body.find("BUILDFLAG(IS_WIN)")
    scope = body[win:] if win >= 0 else body
    match = re.search(r'L"((?:[^"\\]|\\.)*)"', scope)
    if not match:
        raise BootstrapVectorError("%s has no wide-string literal"
                                   % function_name)
    return _unescape(match.group(1), C_ESCAPES)


def _cpp_char_assignment(text, function_name, target):
    """Return the character literal assigned to `target[i]` in a function."""
    body = _function_body(text, function_name)
    match = re.search(r"%s\s*\[\s*0\s*\]\s*=\s*'((?:[^'\\]|\\.)*)'"
                      % re.escape(target), body)
    if not match:
        raise BootstrapVectorError("%s no longer sets %s[0]" % (function_name,
                                                                target))
    return _unescape(match.group(1), C_ESCAPES)


def kernel_hello_keys(text):
    """Return the JSON keys the kernel writes into the Hello payload.

    The keys are written as C-escaped quotes because they sit inside a
    string literal that is itself a JSON document.
    """
    body = _function_body(text, "SendHello")
    keys = set(re.findall(r'\\"([A-Za-z0-9_]+)\\"\s*:', body))
    if not keys:
        raise BootstrapVectorError("the kernel's Hello payload has no keys")
    return frozenset(keys)


def client_hello_keys(text):
    """Return the JSON keys the client looks up in the Hello payload."""
    body = _function_body(text, "parse_hello", rust=True)
    keys = set(re.findall(r"\.get\(\s*\"([A-Za-z0-9_]+)\"\s*\)", body))
    if not keys:
        raise BootstrapVectorError("the client's Hello parser reads no keys")
    return frozenset(keys)


def kernel_hello_nonce_and_version(text):
    """Return the Hello keys in the order the kernel writes them.

    Order is not part of the constant (JSON objects are unordered), but
    reading it lets a human see at a glance that both names are there.
    """
    body = _function_body(text, "SendHello")
    return tuple(re.findall(r'\\"([A-Za-z0-9_]+)\\"\s*:', body))


# ---------------------------------------------------------------------------
# The contract
# ---------------------------------------------------------------------------

# Row label -> role. The labels are prose, so the roles are matched on a
# substring of the label rather than on the label as a whole. The longer
# labels are tested first: "机器码密钥字节" contains "密钥字节", so a
# shorter-first order would file it as the session key.
ROLE_KEYWORDS = (
    ("机器码密钥字节", "machine key bytes"),
    ("会话密钥字节", "session key bytes"),
    ("Hello nonce 字节", "hello nonce bytes"),
    ("GCM nonce 字节", "gcm nonce bytes"),
    ("端点前缀", "endpoint prefix"),
    ("开关名", "pipe switch name"),
    ("HKDF", "hkdf info"),
    ("Windows 管道根", "windows pipe root"),
    ("POSIX 抽象标记", "posix abstract marker"),
    ("Hello JSON 键", "hello json keys"),
)

INT_ROLES = {"session key bytes", "machine key bytes", "hello nonce bytes",
             "gcm nonce bytes"}


def contract_rows(text):
    """Return the contract's bootstrap constants, keyed by role.

    The table has one row per constant. The first column says which
    constant the row is, the second is its value, and the last two name
    the two sides' spellings. The value is read *literally*: the contract
    says the Windows pipe root row holds the bytes as written, so
    unescaping it here would strip the very backslashes being pinned.
    """
    section = _section(text, CONTRACT_HEADING, "bootstrap constants")
    rows = {}
    for line in section.splitlines():
        stripped = line.strip()
        if not stripped.startswith("|"):
            continue
        cells = [cell.strip() for cell in stripped.strip("|").split("|")]
        if len(cells) < 4:
            continue
        label, value = cells[0], cells[1]
        if set(label) <= set("-: "):
            continue
        role = None
        for keyword, name in ROLE_KEYWORDS:
            if keyword in label:
                role = name
                break
        if role is None:
            continue
        if not (value.startswith("`") and value.endswith("`")):
            raise BootstrapVectorError(
                "the contract row for %s has no literal value" % role)
        literal = value[1:-1]
        if role in INT_ROLES:
            if not re.fullmatch(r"\d+", literal):
                raise BootstrapVectorError(
                    "the contract row for %s is not a number: %r"
                    % (role, literal))
            rows[role] = int(literal)
        elif role == "posix abstract marker":
            if literal != NUL_SENTINEL:
                raise BootstrapVectorError(
                    "the contract's POSIX marker is not %s" % NUL_SENTINEL)
            rows[role] = "\x00"
        elif role == "hello json keys":
            rows[role] = frozenset(part.strip()
                                   for part in literal.split(",") if part.strip())
        else:
            rows[role] = literal
    missing = {name for _, name in ROLE_KEYWORDS} - set(rows)
    if missing:
        raise BootstrapVectorError(
            "the contract's bootstrap table is missing %s"
            % ", ".join(sorted(missing)))
    return rows


# ---------------------------------------------------------------------------
# The comparison
# ---------------------------------------------------------------------------

def _normalise_switch(spelling):
    """Reduce either side's switch spelling to the switch itself.

    The kernel holds the bare name it hands to base::CommandLine; the
    client holds the `--name=` prefix it appends to the command line.
    Both denote one switch, so both are reduced to the name.
    """
    text = spelling
    if text.startswith("--"):
        text = text[2:]
    if text.endswith("="):
        text = text[:-1]
    return text


def compare_bootstrap(kernel_root, client_root):
    """Compare the three sides' bootstrap layer.

    Returns a list of :class:`BootstrapCheck`. A side that cannot be read
    raises :class:`BootstrapVectorError` rather than producing a check
    that quietly omits it -- an absent check looks exactly like a passing
    one.
    """
    kernel_root = pathlib.Path(kernel_root)
    client_root = pathlib.Path(client_root)

    def read(root, relative):
        path = root / relative
        if not path.is_file():
            raise BootstrapVectorError("cannot read %s" % path)
        return path.read_text(encoding="utf-8")

    contract_text = contract_path(kernel_root).read_text(encoding="utf-8")

    kernel_client_h = read(kernel_root, KERNEL_CLIENT_H_REL)
    kernel_client_cc = read(kernel_root, KERNEL_CLIENT_CC_REL)
    kernel_keys_h = read(kernel_root, KERNEL_KEYS_H_REL)
    kernel_envelope_h = read(kernel_root, KERNEL_ENVELOPE_H_REL)
    kernel_pipe_cc = read(kernel_root, KERNEL_PIPE_CC_REL)

    client_launch = read(client_root, CLIENT_LAUNCH_REL)
    client_keys = read(client_root, CLIENT_KEYS_REL)
    client_handshake = read(client_root, CLIENT_HANDSHAKE_REL)

    contract = contract_rows(contract_text)

    kernel = {
        "endpoint prefix": _cpp_string(kernel_client_h,
                                       "kKernelPipeEndpointPrefix"),
        "pipe switch name": _normalise_switch(
            _cpp_string(kernel_client_h, "kKernelPipeSwitch")),
        "hkdf info": _cpp_string(kernel_keys_h, "kKernelPipeSessionInfo"),
        "session key bytes": _cpp_size(kernel_keys_h,
                                       "kKernelPipeSessionKeyBytes"),
        "machine key bytes": _cpp_size(kernel_keys_h,
                                       "kKernelPipeMachineKeyBytes"),
        "hello nonce bytes": _cpp_size(kernel_keys_h,
                                       "kKernelPipeHelloNonceBytes"),
        "gcm nonce bytes": _cpp_size(kernel_envelope_h,
                                     "kKernelPipeGcmNonceBytes"),
        "windows pipe root": _cpp_wide_prefix(kernel_client_cc,
                                              "OpenEndpoint"),
        "posix abstract marker": _cpp_char_assignment(kernel_client_cc,
                                                      "OpenEndpoint", "path"),
        "hello json keys": kernel_hello_keys(kernel_pipe_cc),
    }

    client = {
        "endpoint prefix": _rust_str(client_launch, "ENDPOINT_PREFIX"),
        "pipe switch name": _normalise_switch(
            _rust_str(client_launch, "PIPE_ARG_PREFIX")),
        "hkdf info": _rust_str(client_keys, "SESSION_KEY_INFO"),
        "session key bytes": _rust_usize(client_keys, "SESSION_KEY_LEN"),
        "machine key bytes": _rust_usize(client_keys, "K_MACHINE_LEN"),
        "hello nonce bytes": _rust_usize(client_keys, "HELLO_NONCE_LEN"),
        "gcm nonce bytes": _rust_usize(client_keys, "GCM_NONCE_LEN"),
        "windows pipe root": _format_prefix(client_launch,
                                            "windows_endpoint"),
        "posix abstract marker": _format_prefix(client_launch,
                                                "posix_endpoint"),
        "hello json keys": client_hello_keys(client_handshake),
    }

    checks = []
    for _, role in ROLE_KEYWORDS:
        checks.append(BootstrapCheck(role, {
            "contract": contract[role],
            "kernel": kernel[role],
            "client": client[role],
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
        checks = compare_bootstrap(args.kernel, args.client)
    except (OSError, BootstrapVectorError) as error:
        print("cannot read the bootstrap layer: %s" % error)
        return 2

    ok = True
    for check in checks:
        print("%-22s %s" % (check.label, "OK" if check.ok else "MISMATCH"))
        if not check.ok:
            ok = False
            for name, value in check.values.items():
                print("    %-9s %r" % (name, value))
    print()
    print("cross-repo bootstrap layer agrees:", "YES" if ok else "NO")
    return 0 if ok else 1


if __name__ == "__main__":
    raise SystemExit(main())
