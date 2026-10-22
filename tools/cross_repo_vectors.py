"""Cross-repo check: the fixed test vectors must match byte for byte.

Why this exists
---------------
`docs/kernel-wire-contract.md` is the only agreement between the kernel
repo and the client repo. It pins a fixed test vector so the two sides
can prove they encode the same bytes -- the contract's own words: "the
only way to align across repos is the fixed test vectors".

A prose description of that vector is not enough. The contract once
wrote the 45-byte ciphertext as two runs of 63 and 27 hex characters.
Each run is an odd number of characters, so a reader that parses the
block line by line drops the trailing nibble and ends up with 44 bytes
-- while a reader that concatenates first gets the correct 45. Both
sides had implemented the vector correctly, but the document alone
could not tell them how to read it.

This script performs that check against the real files, so a contract
edit that breaks the reading convention is caught before the two repos
disagree on the wire. It is read-only: it never writes to either repo.

What it compares
----------------
The envelope's own bytes (ciphertext, tag, AAD, payload), the runner's
ConfigBundle payload, and the three derived values the handshake is keyed
on: `machine_code`, `k_machine` and `session_key`.

The last group was added after the same trap was found twice. Each repo
hardcodes all three and asserts that its own derivation produces them, so
each side checks itself -- but nothing compared the two copies. Change the
JSON key order on one side and both sides stay green while they no longer
agree on the wire. The contract is read as a third side of the comparison,
so a value that moves in one repo is caught even when the other two still
agree with each other.

Usage
-----
    python3 tools/cross_repo_vectors.py --kernel <kernel-repo> \\
        --client <client-repo>

Both arguments are paths to working copies. The script reads:

* the contract               (kernel-repo/docs/kernel-wire-contract.md,
                               or the out-of-repository copy the build
                               machine keeps -- see `contract_path`)
* the client test vectors    (client-repo/crates/lengsuo-desktop/src/kernel_pipe/tests.rs)
* the kernel test vectors    (kernel-repo/native/kernel_pipe_test_util.h
                               or native/kernel_pipe_test.cc -- the first
                               candidate that carries the literal wins)

It exits non-zero when the three disagree. A side that cannot be read at
all is reported as an error, never skipped: an absent check looks exactly
like a passing one.
"""
from __future__ import annotations

import argparse
import importlib.util
import os
import pathlib
import re
import sys

CONTRACT_REL = "docs/kernel-wire-contract.md"
CLIENT_REL = "crates/lengsuo-desktop/src/kernel_pipe/tests.rs"

# Identifies this module to the frame-layer check, which lives in
# `cross_repo_frames.py` and reuses `contract_path` from here. The two
# checks must read the contract through one lookup, or they could
# disagree about which document they are checking.
TOOL_NAME = "cross_repo_vectors"

# The client repo's home for task R's cross-repo vectors. The ConfigBundle
# payload lives in its own file because it is a wire vector, not a test
# helper: the client asserts against it, and the kernel repo compares
# against it too.
CLIENT_PROFILE_REL = (
    "crates/lengsuo-desktop/src/kernel_pipe/profile_vectors.rs")

# The kernel side's copy of the same payload, and its length in bytes.
# The length is the frame header's `len` field, so it is pinned twice:
# once here, and once by the kernel's own gtest.
PROFILE_PAYLOAD_LEN = 155

# Where the wire contract lives when the repository does not ship it.
#
# The repository keeps `docs/` as a local-only working area, so a clone of
# the kernel repository has no `docs/kernel-wire-contract.md` at all. The
# contract is not that kind of document: it is the single agreement between
# the two repositories, so it is kept outside the repository in a stable
# directory on the build machine. Look in the repository first, then there.
CONTRACT_DIR = os.environ.get(
    "LENGSUO_L3B_BRIEF_DIR", "/home/ubuntu/l3b-briefs")


def contract_path(kernel: pathlib.Path) -> pathlib.Path:
    """Return the contract file, wherever this deployment keeps it."""
    in_repo = kernel / CONTRACT_REL
    if in_repo.is_file():
        return in_repo
    outside = pathlib.Path(CONTRACT_DIR) / pathlib.Path(CONTRACT_REL).name
    if outside.is_file():
        return outside
    # Return the in-repository path so the error message names the place
    # the caller would expect to find it.
    return in_repo

# The kernel side's vector literal is looked up by scanning the candidates
# below, not by a single hard-coded path. Task Q split the pipe code into
# five layers and moved the shared constants into a header
# (`native/kernel_pipe_test_util.h`) because a constant that lives in a .cc
# can only be linked into one test binary. A single-path lookup then failed
# with "the kernel test lost the kEnvelopePayload literal" even though the
# vector was present and byte-identical -- the tool, not the vector, was
# broken. The first candidate that yields the literal wins, and the list is
# scanned in this order so the most specific location comes first.
KERNEL_CANDIDATES = (
    "native/kernel_pipe_test_util.h",
    "native/kernel_pipe_test.cc",
    "native/kernel_pipe_envelope_test.cc",
)

# The layout the contract fixes: the payload's clear region carries six
# fields, and the container id and the expiry live only inside the
# ciphertext's plaintext. The offsets below are the ones the contract
# states, so a drift in the document shows up here as a mismatch.
CIPHERTEXT_OFFSET = 20
CIPHERTEXT_LEN = 45
TAG_OFFSET = 69
TAG_LEN = 16
PAYLOAD_LEN = 85

# The clear region must not repeat the two values that are already
# encrypted. Their plaintext forms are "ct-01" and 1700000000.
CLEAR_REGION_FORBIDDEN = ("63742d3031", "000000006553f100")


class VectorError(Exception):
    """The vector could not be read at all."""


def _frame_tool():
    """Load the frame-layer check, which lives next to this file.

    It is a separate module because this one is already near the size
    limit, and it is loaded by path rather than by name because `tools/`
    is not a package and this file is loaded by path from the tests.
    """
    path = pathlib.Path(__file__).resolve().parent / "cross_repo_frames.py"
    spec = importlib.util.spec_from_file_location("cross_repo_frames", path)
    module = importlib.util.module_from_spec(spec)
    spec.loader.exec_module(module)
    return module


def _rules_tool():
    """Load the rules-layer check, which lives next to this file.

    Same reason as the frame layer: this file is already near the size
    limit, and the module is loaded by path because `tools/` is not a
    package and this file is loaded by path from the tests.
    """
    path = pathlib.Path(__file__).resolve().parent / "cross_repo_rules.py"
    spec = importlib.util.spec_from_file_location("cross_repo_rules", path)
    module = importlib.util.module_from_spec(spec)
    spec.loader.exec_module(module)
    return module


def hex_tokens(text: str) -> str:
    """Return the hex blob in `text` as lowercase byte pairs.

    Split on whitespace first and take each token two characters at a
    time. Pairing characters blindly (a regex over one long run) pairs
    from the left and silently drops a trailing nibble when a line
    break landed mid-byte -- the exact defect this script exists to
    catch, so the reader here must not reproduce it.

    The first token that is not pure hex ends the blob: prose such as
    "（帧头 8 + hello nonce 8）" follows the bytes on a label line.
    """
    out: list[str] = []
    for token in re.split(r"[\s,]+", text):
        token = token.strip("\"'();")
        if not token:
            continue
        if not re.fullmatch(r"[0-9a-fA-F]+", token):
            break
        if len(token) % 2:
            raise VectorError("odd-length hex token: %r" % token)
        out.extend(token[i:i + 2] for i in range(0, len(token), 2))
    return "".join(out).lower()


def contract_blocks(text: str) -> dict[str, str]:
    """Extract the vector blocks from the contract document.

    A block is a label line ("密文 = ...") plus the indented lines that
    continue it. Only lines that consist of hex and whitespace count as
    continuations, so a prose line cannot contribute stray digits.
    """
    if "#### KeyEnvelope 固定测试向量" not in text:
        raise VectorError("the contract lost its fixed-vector section")
    body = text.split("#### KeyEnvelope 固定测试向量", 1)[1].split("## 4.", 1)[0]
    lines = body.splitlines()
    blocks: dict[str, str] = {}
    for prefix, name in (("密文 =", "ciphertext"), ("tag  =", "tag"),
                         ("AAD  =", "aad"), ("载荷 =", "payload")):
        start = next((i for i, line in enumerate(lines)
                      if line.strip().startswith(prefix)), None)
        if start is None:
            raise VectorError("the contract lost the %r block" % prefix)
        parts: list[str] = []
        for index in range(start, len(lines)):
            line = lines[index]
            if index > start:
                if not line.strip() or not line[:1].isspace():
                    break  # a new label line ends the block
                if not re.fullmatch(r"[0-9a-fA-F\s]+", line):
                    break
            parts.append(line.split("=", 1)[1] if "=" in line else line)
        blocks[name] = hex_tokens(" ".join(parts))
    return blocks


def rust_constants(text: str) -> dict[str, str]:
    """Extract the client's byte-array constants."""
    out: dict[str, str] = {}
    for name in ("ENVELOPE_CIPHERTEXT", "ENVELOPE_TAG", "ENVELOPE_AAD"):
        match = re.search(r"const %s: \[u8; (\d+)\] = \[(.*?)\];"
                          % name, text, re.S)
        if not match:
            raise VectorError("the client test lost the %r constant" % name)
        out[name] = "".join(re.findall(r"0x([0-9a-fA-F]{2})",
                                       match.group(2))).lower()
    return out


def cpp_payload(text: str) -> str:
    """Extract the kernel side's hard-coded payload.

    The literal is declared as a `char[]` constant. The declaration has
    taken more than one form in this repo -- `const char` when the
    constant lived in the test source, `inline constexpr char` when the
    pipe code moved the shared constants into a header -- so the storage
    class and the `constexpr` are both optional here. Requiring one exact
    spelling made the tool report a missing vector for a vector that was
    present and byte-identical.
    """
    match = re.search(
        r"(?:inline\s+)?(?:constexpr|const)\s+char\s+kEnvelopePayload\[\]\s*="
        r"\s*(.*?);", text, re.S)
    if not match:
        raise VectorError("the kernel test lost the kEnvelopePayload literal")
    return hex_tokens(match.group(1))


def find_kernel_payload(kernel: pathlib.Path,
                        override: str | None = None) -> str:
    """Read the kernel side's payload literal from the first place it lives.

    The literal is named `kEnvelopePayload` and holds the 85-byte payload
    as a hex string. It may sit in the test source itself or in a shared
    header, depending on how the pipe code is layered, so the lookup scans
    the candidates rather than assuming one layout.
    """
    if override:
        paths = [pathlib.Path(override)]
    else:
        paths = [kernel / rel for rel in KERNEL_CANDIDATES]
    for path in paths:
        if not path.is_file():
            continue
        try:
            return cpp_payload(path.read_text(encoding="utf-8"))
        except VectorError:
            continue
    raise VectorError(
        "no kernel file among %s carries the kEnvelopePayload literal"
        % ", ".join(str(p) for p in paths))


def _raw_strings(text: str, name: str) -> str:
    """Return the raw string literal(s) a Rust constant is built from.

    The client declares the payload with `concat!` over several raw
    string pieces, so the pieces are joined in the order they appear.
    """
    match = re.search(r"const\s+%s\s*:\s*&str\s*=\s*(.*?);" % name, text, re.S)
    if not match:
        raise VectorError("the client lost the %r constant" % name)
    body = match.group(1)
    pieces = re.findall(r'r#"(.*?)"#', body, re.S)
    if not pieces:
        raise VectorError("the client's %r constant has no raw string" % name)
    return "".join(pieces)


def rust_profile_payload(text: str) -> str:
    """Extract the client's ConfigBundle payload as raw bytes.

    The payload is a JSON string, so it is compared as text rather than
    as hex: the two sides must spell the same JSON, and hex-encoding it
    here would only move the question to the encoder.
    """
    return _raw_strings(text, "CONFIG_BUNDLE_PAYLOAD").encode("utf-8").hex()


def cpp_profile_payload(text: str) -> str:
    """Extract the kernel side's ConfigBundle payload as raw bytes.

    The kernel declares it as a C++ raw string literal next to the test
    that checks it, so the literal is read straight out of the source.
    """
    match = re.search(
        r'constexpr\s+char\s+kRunnerPayload\[\]\s*=\s*R"\((.*?)\)";', text, re.S)
    if not match:
        raise VectorError("the kernel test lost the kRunnerPayload literal")
    return match.group(1).encode("utf-8").hex()


def find_kernel_profile_payload(kernel: pathlib.Path) -> str:
    """Read the kernel side's ConfigBundle payload literal.

    The literal lives in the profile bundle's test file, next to the test
    that asserts against it.
    """
    path = kernel / "native" / "profile_bundle_test.cc"
    if not path.is_file():
        raise VectorError("the kernel repo has no %s" % path)
    return cpp_profile_payload(path.read_text(encoding="utf-8"))


# The three derived values the wire contract pins, and the name each side
# gives the constant that holds it.
#
# These are the values the handshake is keyed on: the machine code fixes the
# JSON shape and the key order, `k_machine` is the digest of that code's
# ASCII bytes, and the session key is the HKDF output. A change to any of
# them on one side only is a silent wire break, and each side's own test
# cannot see it -- each asserts its own output against its own copy of the
# constant, so the two copies drift apart while both stay green.
#
# The kernel side spells them `kMachineCode`, `kMachineKeyHex` and
# `kSessionKeyHex`; the client spells them `MACHINE_CODE`, `K_MACHINE_HEX`
# and `SESSION_KEY_HEX`. The names are listed here so the readers below
# stay independent of either spelling.
MACHINE_CODE_NAMES = (
    ("machine_code", "kMachineCode", "MACHINE_CODE"),
    ("k_machine", "kMachineKeyHex", "K_MACHINE_HEX"),
    ("session_key", "kSessionKeyHex", "SESSION_KEY_HEX"),
)


def _kernel_machine_code(text: str) -> dict[str, str]:
    """Extract the kernel side's three derived values.

    The kernel declares them as `char[]` constants, the same way it
    declares the envelope payload, and the declaration has taken both the
    `const char` and the `inline constexpr char` forms. The storage class
    and the `constexpr` are therefore both optional here.
    """
    out: dict[str, str] = {}
    for name, kernel_name, _ in MACHINE_CODE_NAMES:
        match = re.search(
            r"(?:inline\s+)?(?:constexpr|const)\s+char\s+%s\[\]\s*=\s*"
            r'"([0-9a-fA-F]+)";' % kernel_name, text)
        if not match:
            raise VectorError("the kernel test lost the %r literal"
                              % kernel_name)
        out[name] = match.group(1).lower()
    return out


def find_kernel_machine_code(kernel: pathlib.Path) -> dict[str, str]:
    """Read the kernel side's three derived values.

    The literals live in the shared test header, next to the envelope
    payload, so the lookup scans the same candidates.
    """
    paths = [kernel / rel for rel in KERNEL_CANDIDATES]
    for path in paths:
        if not path.is_file():
            continue
        try:
            return _kernel_machine_code(path.read_text(encoding="utf-8"))
        except VectorError:
            continue
    raise VectorError(
        "no kernel file among %s carries the machine-code literals"
        % ", ".join(str(p) for p in paths))


def rust_machine_code(text: str) -> dict[str, str]:
    """Extract the client side's three derived values.

    The client declares them as `&str` constants, each on its own line
    with the value in a plain string literal.
    """
    out: dict[str, str] = {}
    for name, _, client_name in MACHINE_CODE_NAMES:
        match = re.search(
            r'const\s+%s\s*:\s*&str\s*=\s*"([0-9a-fA-F]+)";' % client_name,
            text)
        if not match:
            raise VectorError("the client lost the %r constant" % client_name)
        out[name] = match.group(1).lower()
    return out


def contract_machine_code(text: str) -> dict[str, str]:
    """Extract the three derived values from the contract document.

    The contract states each value as a label line followed by a line that
    carries the value after an equals sign:

        machine_code = SHA256(UTF-16LE(上述 JSON)) 的十六进制小写
                     = 216feaf8...

    so the value is found by reading the line after the label, not by
    matching the label and the value in one pattern. The values are read
    from the derivation section only, so a value that happens to appear in
    the prose elsewhere cannot be mistaken for the pinned one.
    """
    if "密钥派生固定测试向量" not in text:
        raise VectorError("the contract lost its derivation-vector section")
    body = text.split("密钥派生固定测试向量", 1)[1]
    lines = body.splitlines()
    out: dict[str, str] = {}
    for name in ("machine_code", "k_machine", "session_key"):
        start = next((i for i, line in enumerate(lines)
                      if line.strip().startswith(name)), None)
        if start is None:
            raise VectorError("the contract lost the %r value" % name)
        value = None
        for line in lines[start + 1:start + 3]:
            match = re.fullmatch(r"\s*=\s*([0-9a-fA-F]{64})\s*", line)
            if match:
                value = match.group(1).lower()
                break
        if value is None:
            raise VectorError("the contract lost the %r value" % name)
        out[name] = value
    return out


def contract_machine_json(text: str) -> str:
    """Extract the machine-code JSON the contract derives from.

    The JSON is the one line in the derivation section that starts with a
    brace, so it is read as a whole line rather than parsed out of the
    prose around it.
    """
    if "密钥派生固定测试向量" not in text:
        raise VectorError("the contract lost its derivation-vector section")
    body = text.split("密钥派生固定测试向量", 1)[1]
    for line in body.splitlines():
        stripped = line.strip()
        if stripped.startswith("{") and stripped.endswith("}"):
            return stripped
    raise VectorError("the contract lost the machine-code JSON")


def main(argv: list[str] | None = None) -> int:
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("--kernel", required=True,
                        help="path to the kernel repo working copy")
    parser.add_argument("--client", required=True,
                        help="path to the client repo working copy")
    parser.add_argument("--kernel-test", default=None,
                        help="path to the kernel side test file; defaults to "
                             "scanning the known kernel vector locations under "
                             "<kernel>/native/. Point it at a card's working "
                             "copy when the card has not been merged yet.")
    args = parser.parse_args(argv)

    kernel = pathlib.Path(args.kernel)
    client = pathlib.Path(args.client)

    try:
        contract = contract_blocks(
            contract_path(kernel).read_text(encoding="utf-8"))
        client_const = rust_constants(
            (client / CLIENT_REL).read_text(encoding="utf-8"))
        kernel_payload = find_kernel_payload(kernel, args.kernel_test)
    except (OSError, VectorError) as error:
        print("cannot read the vectors: %s" % error, file=sys.stderr)
        return 2

    client_ct = client_const["ENVELOPE_CIPHERTEXT"]
    client_tag = client_const["ENVELOPE_TAG"]
    client_aad = client_const["ENVELOPE_AAD"]
    kernel_ct = kernel_payload[CIPHERTEXT_OFFSET * 2:
                               (CIPHERTEXT_OFFSET + CIPHERTEXT_LEN) * 2]
    kernel_tag = kernel_payload[TAG_OFFSET * 2:(TAG_OFFSET + TAG_LEN) * 2]

    checks = [
        ("ciphertext (%d B)" % CIPHERTEXT_LEN,
         contract["ciphertext"], client_ct, kernel_ct),
        ("tag (%d B)" % TAG_LEN,
         contract["tag"], client_tag, kernel_tag),
        ("AAD (16 B)",
         contract["aad"], client_aad, None),
        ("payload (%d B)" % PAYLOAD_LEN,
         contract["payload"], None, kernel_payload),
    ]

    ok = True
    for label, *values in checks:
        present = [v for v in values if v is not None]
        agree = all(v == present[0] for v in present)
        lengths = ", ".join("%d B" % (len(v) // 2) for v in present)
        print("%-22s %-18s %s" % (label, lengths, "OK" if agree else "MISMATCH"))
        if not agree:
            ok = False
            for name, value in zip(("contract", "client", "kernel"), values):
                if value is not None:
                    print("    %-9s %s" % (name, value))

    print()
    for forbidden in CLEAR_REGION_FORBIDDEN:
        clean = forbidden not in kernel_payload
        print("clear region omits %-18s %s"
              % (forbidden, "OK" if clean else "LEAKED"))
        ok = ok and clean

    # Task R's ConfigBundle payload. The kernel parses it, the client
    # sends it, and the two repos each hardcode their own copy -- so the
    # copies are compared here, the same way the envelope vectors are.
    # A missing side is reported, not skipped: a silently absent check
    # would look like a passing one.
    print()
    try:
        kernel_profile = find_kernel_profile_payload(kernel)
        client_profile_file = client / CLIENT_PROFILE_REL
        if not client_profile_file.is_file():
            raise VectorError("the client repo has no %s" % client_profile_file)
        client_profile = rust_profile_payload(
            client_profile_file.read_text(encoding="utf-8"))
    except (OSError, VectorError) as error:
        print("ConfigBundle payload  %s" % error)
        ok = False
    else:
        agree = kernel_profile == client_profile
        print("ConfigBundle payload  %-18s %s"
              % ("%d B" % (len(kernel_profile) // 2),
                 "OK" if agree else "MISMATCH"))
        if not agree:
            ok = False
            print("    %-9s %s" % ("client", client_profile))
            print("    %-9s %s" % ("kernel", kernel_profile))

    # The three derived values the handshake is keyed on. Both repos
    # hardcode them and each asserts its own output against its own copy,
    # so nothing catches a copy that drifts -- which is the trap task R
    # fell into with the ConfigBundle payload. The contract is the third
    # side of the comparison: a value that moves in one repo shows up even
    # when the other two still agree with each other.
    #
    # A missing side is reported, not skipped: an absent check would look
    # exactly like a passing one.
    print()
    try:
        kernel_machine = find_kernel_machine_code(kernel)
        client_machine = rust_machine_code(
            (client / CLIENT_REL).read_text(encoding="utf-8"))
        contract_machine = contract_machine_code(
            contract_path(kernel).read_text(encoding="utf-8"))
    except (OSError, VectorError) as error:
        print("machine code           %s" % error)
        ok = False
    else:
        for name, _, _ in MACHINE_CODE_NAMES:
            values = (contract_machine[name], client_machine[name],
                      kernel_machine[name])
            agree = len(set(values)) == 1
            print("%-22s %-18s %s" % (name, "64 B",
                                      "OK" if agree else "MISMATCH"))
            if not agree:
                ok = False
                for label, value in zip(("contract", "client", "kernel"),
                                        values):
                    print("    %-9s %s" % (label, value))

    # The frame layer: the header layout, the payload ceiling, the
    # protocol version, the frame type codes and the fixed legal/illegal
    # frame vectors. Both repos hardcode their own copy of each, and
    # each side only checks its own copy against its own output -- the
    # same trap task R fell into with the ConfigBundle payload, one
    # layer down. So the frame layer gets the same treatment: three-way
    # comparison with the contract as the third side.
    #
    # A missing side is reported, not skipped: an absent check looks
    # exactly like a passing one.
    print()
    frame_tool = _frame_tool()
    try:
        frame_checks = frame_tool.compare_frames(kernel, client)
    except (OSError, frame_tool.FrameVectorError) as error:
        print("frame layer            %s" % error)
        ok = False
    else:
        for check in frame_checks:
            print("%-22s %-18s %s" % (check.label, "",
                                      "OK" if check.ok else "MISMATCH"))
            if not check.ok:
                ok = False
                for name, value in check.values.items():
                    print("    %-9s %s" % (name, value))

    # The profile sync rules layer: the config section name, the package
    # ceiling, the two patterns that never leave the machine, and the
    # glob fixed vectors. Both repos hardcode their own copy of each --
    # the kernel in `native/profile_bundle.*`, the client in
    # `.../kernel_pipe/profile_rules.rs` -- and each side only checks its
    # own copy against its own output. The same trap, one layer further
    # down, and this layer is the privacy floor: "Cookies and Network
    # Cache never leave the machine" is two separate promises if the two
    # copies of the floor patterns differ.
    #
    # A missing side is reported, not skipped: an absent check looks
    # exactly like a passing one.
    print()
    rules_tool = _rules_tool()
    try:
        rules_checks = rules_tool.compare_rules(kernel, client)
    except (OSError, rules_tool.RuleVectorError) as error:
        print("rules layer            %s" % error)
        ok = False
    else:
        for check in rules_checks:
            print("%-22s %-18s %s" % (check.label, "",
                                      "OK" if check.ok else "MISMATCH"))
            if not check.ok:
                ok = False
                for name, value in check.values.items():
                    print("    %-9s %s" % (name, value))

    print()
    print("cross-repo vectors agree:" , "YES" if ok else "NO")
    return 0 if ok else 1
if __name__ == "__main__":
    sys.exit(main())
