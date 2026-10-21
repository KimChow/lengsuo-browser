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

Usage
-----
    python3 tools/cross_repo_vectors.py --kernel <kernel-repo> \\
        --client <client-repo>

Both arguments are paths to working copies. The script reads:

* the contract               (kernel-repo/docs/kernel-wire-contract.md)
* the client test vectors    (client-repo/crates/lengsuo-desktop/src/kernel_pipe/tests.rs)
* the kernel test vectors    (kernel-repo/native/kernel_pipe_test.cc)

It exits non-zero when the three disagree.
"""
from __future__ import annotations

import argparse
import pathlib
import re
import sys

CONTRACT_REL = "docs/kernel-wire-contract.md"
CLIENT_REL = "crates/lengsuo-desktop/src/kernel_pipe/tests.rs"
KERNEL_REL = "native/kernel_pipe_test.cc"

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
    """Extract the kernel side's hard-coded payload."""
    match = re.search(r"const char kEnvelopePayload\[\] =\s*(.*?);", text, re.S)
    if not match:
        raise VectorError("the kernel test lost the kEnvelopePayload literal")
    return hex_tokens(match.group(1))


def main(argv: list[str] | None = None) -> int:
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("--kernel", required=True,
                        help="path to the kernel repo working copy")
    parser.add_argument("--client", required=True,
                        help="path to the client repo working copy")
    parser.add_argument("--kernel-test", default=None,
                        help="path to the kernel side test file; defaults to "
                             "<kernel>/native/kernel_pipe_test.cc. Point it at "
                             "a card's working copy when the card has not "
                             "been merged yet.")
    args = parser.parse_args(argv)

    kernel = pathlib.Path(args.kernel)
    client = pathlib.Path(args.client)
    kernel_test = (pathlib.Path(args.kernel_test) if args.kernel_test
                   else kernel / KERNEL_REL)

    try:
        contract = contract_blocks(
            (kernel / CONTRACT_REL).read_text(encoding="utf-8"))
        client_const = rust_constants(
            (client / CLIENT_REL).read_text(encoding="utf-8"))
        kernel_payload = cpp_payload(kernel_test.read_text(encoding="utf-8"))
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

    print()
    print("cross-repo vectors agree:" , "YES" if ok else "NO")
    return 0 if ok else 1


if __name__ == "__main__":
    sys.exit(main())
