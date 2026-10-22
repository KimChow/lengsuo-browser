"""Cross-repo check: the frame layer's constants must match byte for byte.

Why this exists
---------------
`docs/kernel-wire-contract.md` pins the whole frame layer: the header
layout, the payload ceiling, the protocol version, the seven frame type
codes, and the fixed legal/illegal frame vectors. Its own words for the
vectors are "the two sets of constants must be byte for byte identical --
change the protocol and change the vectors on both sides".

Task P built the client's codec and task Q built the kernel's. Each side
hardcoded its own copy of every one of those constants -- the kernel in
`native/kernel_pipe_frame.h` and `native/kernel_pipe_test_util.h`, the
client in `crates/.../kernel_pipe/mod.rs` and `.../handshake.rs` -- and
each side then asserted its own output against its own copy. That proves
nothing about the wire.

This is the same trap task R fell into with the ConfigBundle payload and
the machine code: two copies of one constant, each checked only against
its own output, with nothing comparing the copies. Change a frame type on
one side and both sides stay green while the two sides no longer agree
on the wire.

So the contract is the third side of the comparison, exactly as it is
for the envelope vectors: a value that moves in one repo is caught even
when the other two still agree with each other. A side that cannot be
read is reported, never skipped -- an absent check looks exactly like a
passing one.

What it compares
----------------
* the seven frame type codes        (contract section 3)
* the set of frame types each side accepts
* the frame header length           (contract section 2)
* the payload ceiling               (contract section 2)
* the protocol version              (contract section 1)
* the six legal frame headers       (contract section 2)
* the five illegal frame vectors    (contract section 2)

Usage
-----
    from tools.cross_repo_frames import compare_frames

    for check in compare_frames(kernel_repo, client_repo):
        print(check.label, check.ok, check.values)

This module is read-only: it never writes to either repo.
"""
import importlib.util
import pathlib
import re
import sys

# Where each side keeps the frame layer.
CLIENT_FRAME_REL = "crates/lengsuo-desktop/src/kernel_pipe/mod.rs"
CLIENT_HANDSHAKE_REL = "crates/lengsuo-desktop/src/kernel_pipe/handshake.rs"
KERNEL_FRAME_REL = "native/kernel_pipe_frame.h"
KERNEL_SOURCE_REL = "native/kernel_pipe_frame.cc"
KERNEL_VECTORS_REL = "native/kernel_pipe_test_util.h"

# The canonical frame type codes. The contract, the kernel and the client
# must all agree with this and with each other; the contract is read at
# run time, so this constant is the copy that travels with the code.
CONTRACT_FRAME_TYPES = {
    "Hello": 0x01,
    "KeyEnvelope": 0x02,
    "ConfigBundle": 0x03,
    "ActionRequest": 0x04,
    "ActionResult": 0x05,
    "Event": 0x81,
    "EventAck": 0x82,
}

# The kernel spells its enum members with a `k` prefix; the contract and
# the client do not. The prefix is dropped so the three sides can be
# compared by name.
KERNEL_NAME_PREFIX = "k"

# The contract's headings. A heading that moved is a section that cannot
# be read, and a section that cannot be read is an error -- never a
# silently skipped check.
CONTRACT_FRAME_TYPE_HEADING = "## 3. 帧类型"
CONTRACT_FRAME_SECTION_HEADING = "## 2. 帧格式"

# The fixed vectors live under their own sub-heading inside section 2, so
# the tables are read from there rather than from the section's start.
CONTRACT_VECTOR_HEADING = "#### ⭐ 固定测试向量"


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


_VECTORS = None


def _vectors():
    """The envelope tool, loaded once.

    It owns the lookup for where the contract is kept: the repository
    keeps `docs/` local-only, so on the build machine the contract lives
    in a directory outside the repository. That lookup must have exactly
    one home, or the two checks could read different documents.

    The module is found by path rather than by name because `tools/` is
    not a package and this file is loaded by path from the tests. An
    already-loaded copy is preferred, so running the two checks together
    does not execute the envelope tool twice.
    """
    global _VECTORS
    if _VECTORS is not None:
        return _VECTORS
    for module in (sys.modules.get("__main__"),
                   sys.modules.get("cross_repo_vectors")):
        # The marker is what identifies the envelope tool. Matching on a
        # function name instead would find this module when it is run as
        # a script, and calling into it would recurse forever.
        if module is not None and getattr(module, "TOOL_NAME", None) == (
                "cross_repo_vectors"):
            _VECTORS = module
            return _VECTORS
    _VECTORS = _sibling("cross_repo_vectors")
    return _VECTORS


def contract_path(kernel):
    """Return the contract file, wherever this deployment keeps it."""
    return _vectors().contract_path(kernel)


class FrameVectorError(Exception):
    """The frame layer's constants could not be read at all."""


class FrameCheck:
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
        return "FrameCheck(%r, ok=%r)" % (self.label, self.ok)


# ---------------------------------------------------------------------------
# Small readers
# ---------------------------------------------------------------------------

def _hex_blob(text):
    """Return the hex bytes in `text` as lowercase byte pairs.

    Accepts both spellings the repos use: a C string of bare pairs
    ("00 00 00 03") and a Rust array of 0x literals ("0x00, 0x00"). The
    bytes are taken two characters at a time so a trailing nibble is an
    error rather than a silent drop.
    """
    out = []
    for token in re.split(r"[\s,]+", text):
        token = token.strip("\"'();")
        if not token:
            continue
        if token.lower().startswith("0x"):
            token = token[2:]
        if not re.fullmatch(r"[0-9a-fA-F]+", token):
            break
        if len(token) % 2:
            raise FrameVectorError("odd-length hex token: %r" % token)
        out.extend(token[i:i + 2] for i in range(0, len(token), 2))
    return "".join(out).lower()


def _int_expr(text):
    """Evaluate a constant expression made of integers joined by `*`."""
    parts = [p.strip() for p in text.split("*")]
    value = 1
    for part in parts:
        if not re.fullmatch(r"\d+", part):
            raise FrameVectorError("not an integer expression: %r" % text)
        value *= int(part)
    return value


def _section(text, heading, what):
    """Return the text under `heading`, up to the next heading line."""
    index = text.find(heading)
    if index < 0:
        raise FrameVectorError("the contract lost its %s section" % what)
    kept = []
    for line in text[index + len(heading):].splitlines():
        if line.startswith("#"):
            break
        kept.append(line)
    return "\n".join(kept)


# ---------------------------------------------------------------------------
# The contract
# ---------------------------------------------------------------------------

def contract_frame_types(text):
    """Return the contract's frame type codes, by name."""
    section = _section(text, CONTRACT_FRAME_TYPE_HEADING, "frame type")
    types = {}
    for value, name in re.findall(
            r"^\|\s*`0x([0-9a-fA-F]{2})`\s*\|\s*`([A-Za-z][A-Za-z0-9_]*)`",
            section, re.M):
        types[name] = int(value, 16)
    if not types:
        raise FrameVectorError("the contract lost its frame type table")
    return types


def contract_legal_frames(text):
    """Return the contract's legal frame headers, keyed by payload length.

    The key is the payload length the header declares, which is also the
    row's own length column; the two must agree. The total frame length
    is what makes the header length derivable rather than assumed.
    """
    section = _section(text, CONTRACT_VECTOR_HEADING, "fixed vector")
    frames = {}
    for length, header, total in re.findall(
            r"^\|\s*`V[0-9A-Za-z]+`\s*\|\s*(\d+)\s*\|\s*`([0-9a-fA-F ]+)`\s*\|"
            r"[^|]*\|\s*(\d+)\s*\|", section, re.M):
        blob = _hex_blob(header)
        if len(blob) != 16:
            raise FrameVectorError("a legal frame header is not eight bytes")
        if int(blob[0:8], 16) != int(length):
            raise FrameVectorError(
                "a legal frame header disagrees with its own length column")
        frames[int(length)] = (blob, int(total))
    if not frames:
        raise FrameVectorError("the contract lost its legal frame vectors")
    return frames


def contract_illegal_frames(text):
    """Return the contract's illegal frame vectors, keyed by name."""
    section = _section(text, CONTRACT_VECTOR_HEADING, "fixed vector")
    frames = {}
    for name, blob in re.findall(
            r"^\|\s*`(IL_[A-Z_]+)`\s*\|[^|]*\|\s*`([0-9a-fA-F ]+)`",
            section, re.M):
        frames[name] = _hex_blob(blob)
    if not frames:
        raise FrameVectorError("the contract lost its illegal frame vectors")
    return frames


def contract_frame_constants(text):
    """Return the contract's header length, payload ceiling and version.

    The header length is derived from the legal frame table -- the total
    frame length minus the payload length -- so it is read from the
    document's own numbers rather than from prose.
    """
    legal = contract_legal_frames(text)
    headers = {total - length for length, (_, total) in legal.items()}
    if len(headers) != 1:
        raise FrameVectorError(
            "the contract's legal frames disagree about the header length")
    header_bytes = headers.pop()

    ceiling = re.search(r"\|\s*`len`\s*上限\s*\|\s*(\d+)\s*MiB\s*\|", text)
    if not ceiling:
        raise FrameVectorError("the contract lost its payload ceiling")
    max_payload = int(ceiling.group(1)) * 1024 * 1024
    if max(legal) != max_payload:
        raise FrameVectorError(
            "the contract's largest legal frame is not its stated ceiling")

    version = re.search(
        r"\|\s*`version`\s*\|\s*u32\s*\|[^|]*?当前\s*`(\d+)`", text)
    if not version:
        raise FrameVectorError("the contract lost its protocol version")
    return {
        "header_bytes": header_bytes,
        "max_payload_bytes": max_payload,
        "protocol_version": int(version.group(1)),
    }


# ---------------------------------------------------------------------------
# The kernel side
# ---------------------------------------------------------------------------

def kernel_frame_types(text):
    """Return the kernel's frame type codes, by name."""
    index = text.find("enum class FrameType")
    if index < 0:
        raise FrameVectorError("the kernel lost its FrameType enum")
    body = text[index:]
    body = body.split("};", 1)[0]
    types = {}
    for name, value in re.findall(
            r"\b([A-Za-z][A-Za-z0-9_]*)\s*=\s*(0x[0-9a-fA-F]{2})", body):
        if name.startswith(KERNEL_NAME_PREFIX):
            name = name[len(KERNEL_NAME_PREFIX):]
        types[name] = int(value, 16)
    if not types:
        raise FrameVectorError("the kernel's FrameType enum is empty")
    return types


def kernel_known_frame_types(text):
    """Return the frame type codes the kernel's decoder accepts."""
    index = text.find("IsKnownFrameType")
    if index < 0:
        raise FrameVectorError("the kernel lost IsKnownFrameType")
    body = text[index:]
    body = body.split("\n}", 1)[0]
    values = {int(v, 16) for v in re.findall(r"case\s+(0x[0-9a-fA-F]{2})", body)}
    if not values:
        raise FrameVectorError("IsKnownFrameType accepts no frame type")
    return values


def kernel_frame_constants(text):
    """Return the kernel's header length, payload ceiling and version."""
    header = re.search(
        r"kKernelPipeHeaderBytes\s*=\s*(\d+)\s*;", text)
    ceiling = re.search(
        r"kKernelPipeMaxPayloadBytes\s*=\s*([\d\s*]+);", text)
    version = re.search(
        r"kKernelPipeProtocolVersion\s*=\s*(\d+)\s*;", text)
    if not (header and ceiling and version):
        raise FrameVectorError("the kernel lost its frame constants")
    return {
        "header_bytes": int(header.group(1)),
        "max_payload_bytes": _int_expr(ceiling.group(1)),
        "protocol_version": int(version.group(1)),
    }


def kernel_legal_frames(text):
    """Return the kernel's legal frame headers, keyed by payload length."""
    frames = {}
    for blob in re.findall(
            r"kLegalHeader\w+\[\]\s*=\s*\"([0-9a-fA-F ]+)\"", text):
        hexed = _hex_blob(blob)
        if len(hexed) != 16:
            raise FrameVectorError("a legal frame header is not eight bytes")
        frames[int(hexed[0:8], 16)] = hexed
    if not frames:
        raise FrameVectorError("the kernel lost its legal frame vectors")
    return frames


def kernel_illegal_frames(text):
    """Return the kernel's illegal frame vectors, keyed by name.

    The kernel spells the names in CamelCase while the contract and the
    client spell them in SCREAMING_CASE, so the names are mapped rather
    than compared literally.
    """
    names = {
        "kIllegalLenOver": "IL_LEN_OVER",
        "kIllegalReserved": "IL_RESERVED",
        "kIllegalType": "IL_TYPE",
        "kIllegalTruncHeader": "IL_TRUNC_HDR",
        "kIllegalTruncPayload": "IL_TRUNC_PAYLOAD",
    }
    frames = {}
    for name, blob in re.findall(
            r"(kIllegal\w+)\[\]\s*=\s*\"([0-9a-fA-F ]+)\"", text):
        if name not in names:
            raise FrameVectorError("the kernel has an unknown illegal vector: %s"
                                   % name)
        frames[names[name]] = _hex_blob(blob)
    if not frames:
        raise FrameVectorError("the kernel lost its illegal frame vectors")
    return frames


# ---------------------------------------------------------------------------
# The client side
# ---------------------------------------------------------------------------

def client_frame_types(text):
    """Return the client's frame type codes, by name."""
    index = text.find("pub enum FrameType")
    if index < 0:
        raise FrameVectorError("the client lost its FrameType enum")
    body = text[index:]
    body = body.split("\n}", 1)[0]
    types = {}
    for name, value in re.findall(
            r"^\s*([A-Za-z][A-Za-z0-9_]*)\s*=\s*(0x[0-9a-fA-F]{2})\s*,",
            body, re.M):
        types[name] = int(value, 16)
    if not types:
        raise FrameVectorError("the client's FrameType enum is empty")
    return types


def client_known_frame_types(text):
    """Return the frame type codes the client's decoder accepts."""
    index = text.find("fn from_code")
    if index < 0:
        raise FrameVectorError("the client lost FrameType::from_code")
    body = text[index:]
    body = body.split("\n    }", 1)[0]
    values = {int(v, 16) for v in
              re.findall(r"(0x[0-9a-fA-F]{2})\s*=>\s*Some\(", body)}
    if not values:
        raise FrameVectorError("from_code accepts no frame type")
    return values


def client_frame_constants(text):
    """Return the client's header length and payload ceiling."""
    header = re.search(r"pub const HEADER_LEN:\s*usize\s*=\s*(\d+)\s*;", text)
    ceiling = re.search(
        r"pub const MAX_PAYLOAD_LEN:\s*u32\s*=\s*([\d\s*]+)\s*;", text)
    if not (header and ceiling):
        raise FrameVectorError("the client lost its frame constants")
    return {
        "header_bytes": int(header.group(1)),
        "max_payload_bytes": _int_expr(ceiling.group(1)),
    }


def client_protocol_version(text):
    """Return the protocol version the client sends in Hello."""
    match = re.search(
        r"pub const PROTOCOL_VERSION:\s*u32\s*=\s*(\d+)\s*;", text)
    if not match:
        raise FrameVectorError("the client lost its protocol version")
    return int(match.group(1))


def client_legal_frames(text):
    """Return the client's legal frame headers, keyed by payload length."""
    index = text.find("LEGAL_HEADERS")
    if index < 0:
        raise FrameVectorError("the client lost its legal frame vectors")
    body = text[index:]
    body = body.split("];", 1)[0]
    frames = {}
    for length, blob in re.findall(
            r"\(\s*([\d_]+)\s*,\s*\[([^\]]*)\]\s*\)", body):
        hexed = _hex_blob(blob)
        if len(hexed) != 16:
            raise FrameVectorError("a legal frame header is not eight bytes")
        declared = int(length.replace("_", ""))
        if int(hexed[0:8], 16) != declared:
            raise FrameVectorError(
                "a legal frame header disagrees with its own length")
        frames[declared] = hexed
    if not frames:
        raise FrameVectorError("the client lost its legal frame vectors")
    return frames


def client_illegal_frames(text):
    """Return the client's illegal frame vectors, keyed by name."""
    frames = {}
    for name, blob in re.findall(
            r"const\s+(IL_[A-Z_]+)\s*:\s*\[u8;\s*\d+\]\s*=\s*\[([^\]]*)\]",
            text):
        frames[name] = _hex_blob(blob)
    if not frames:
        raise FrameVectorError("the client lost its illegal frame vectors")
    return frames


# ---------------------------------------------------------------------------
# The comparison
# ---------------------------------------------------------------------------

def compare_frames(kernel_root, client_root):
    """Compare the three sides' frame layer.

    Returns a list of :class:`FrameCheck`. A side that cannot be read
    raises :class:`FrameVectorError` rather than producing a check that
    quietly omits it -- an absent check looks exactly like a passing one.
    """
    kernel_root = pathlib.Path(kernel_root)
    client_root = pathlib.Path(client_root)

    def read(root, relative):
        path = root / relative
        if not path.is_file():
            raise FrameVectorError("cannot read %s" % path)
        return path.read_text(encoding="utf-8")

    contract_text = contract_path(kernel_root).read_text(encoding="utf-8")
    kernel_header = read(kernel_root, KERNEL_FRAME_REL)
    kernel_source = read(kernel_root, KERNEL_SOURCE_REL)
    kernel_vectors = read(kernel_root, KERNEL_VECTORS_REL)
    client_frame = read(client_root, CLIENT_FRAME_REL)
    client_handshake = read(client_root, CLIENT_HANDSHAKE_REL)

    contract_types = contract_frame_types(contract_text)
    contract_constants = contract_frame_constants(contract_text)
    contract_legal = {length: blob
                      for length, (blob, _) in
                      contract_legal_frames(contract_text).items()}
    contract_illegal = contract_illegal_frames(contract_text)

    kernel_types = kernel_frame_types(kernel_header)
    kernel_constants = kernel_frame_constants(kernel_header)
    kernel_legal = kernel_legal_frames(kernel_vectors)
    kernel_illegal = kernel_illegal_frames(kernel_vectors)

    client_types = client_frame_types(client_frame)
    client_constants = client_frame_constants(client_frame)
    client_legal = client_legal_frames(client_frame)
    client_illegal = client_illegal_frames(client_frame)

    checks = [
        FrameCheck("frame types", {
            "contract": contract_types,
            "kernel": kernel_types,
            "client": client_types,
        }),
        FrameCheck("known frame types", {
            "contract": set(contract_types.values()),
            "kernel": kernel_known_frame_types(kernel_source),
            "client": client_known_frame_types(client_frame),
        }),
        FrameCheck("header bytes", {
            "contract": contract_constants["header_bytes"],
            "kernel": kernel_constants["header_bytes"],
            "client": client_constants["header_bytes"],
        }),
        FrameCheck("max payload bytes", {
            "contract": contract_constants["max_payload_bytes"],
            "kernel": kernel_constants["max_payload_bytes"],
            "client": client_constants["max_payload_bytes"],
        }),
        FrameCheck("protocol version", {
            "contract": contract_constants["protocol_version"],
            "kernel": kernel_constants["protocol_version"],
            "client": client_protocol_version(client_handshake),
        }),
        FrameCheck("legal frame headers", {
            "contract": contract_legal,
            "kernel": kernel_legal,
            "client": client_legal,
        }),
        FrameCheck("illegal frame vectors", {
            "contract": contract_illegal,
            "kernel": kernel_illegal,
            "client": client_illegal,
        }),
    ]
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
        checks = compare_frames(args.kernel, args.client)
    except (OSError, FrameVectorError) as error:
        print("cannot read the frame vectors: %s" % error)
        return 2

    ok = True
    for check in checks:
        print("%-22s %s" % (check.label, "OK" if check.ok else "MISMATCH"))
        if not check.ok:
            ok = False
            for name, value in check.values.items():
                print("    %-9s %s" % (name, value))
    print()
    print("cross-repo frame layer agrees:", "YES" if ok else "NO")
    return 0 if ok else 1


if __name__ == "__main__":
    raise SystemExit(main())
