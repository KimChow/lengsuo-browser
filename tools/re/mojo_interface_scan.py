"""Extract the two Mojo method-name tables from the Reference chrome.dll.

Stage 2 of the scan found two distinct string tables in .rdata:

  T1  at ~0x0fae7a..  the sbipc Mojo method names. "AddCanvasFingerprint" sits
      inside a contiguous run of lowerCamelCase / PascalCase identifiers that
      are the methods of the sbipc interfaces (browser <-> renderer).

  T2  at ~0x0f1d94..  the JS binding names: "getCanvasFingerprintingData",
      "getWebGLFingerprintingData" and their siblings. These are the handles the
      injected script calls, so they are the *renderer-side entry points* of the
      fingerprint channel.

This script walks the contiguous identifier runs around each anchor and prints
them in file order with their offsets, so the method list can be read off
reliably instead of guessed.
"""

from __future__ import annotations

import argparse
import collections
import re
import struct

IDENT = re.compile(rb"^[A-Za-z_][A-Za-z0-9_]{2,63}$")


def read_sections(blob: bytes):
    pe_off = struct.unpack_from("<I", blob, 0x3C)[0]
    coff = pe_off + 4
    opt_size = struct.unpack_from("<H", blob, coff + 16)[0]
    sec_off = coff + 20 + opt_size
    out = []
    for i in range(struct.unpack_from("<H", blob, coff + 2)[0]):
        base = sec_off + i * 40
        name = blob[base : base + 8].rstrip(b"\x00").decode("ascii", "replace")
        vsize, vaddr, rawsize, rawptr = struct.unpack_from("<IIII", blob, base + 8)
        out.append((name, rawptr, rawsize, vaddr))
    return out


def section_of(sections, offset: int) -> str:
    for name, rawptr, rawsize, _ in sections:
        if rawptr <= offset < rawptr + rawsize:
            return name
    return "<headers>"


def runs(blob: bytes, min_len: int = 4):
    """Yield (offset, text) for maximal printable ASCII runs."""
    start = None
    for i, byte in enumerate(blob):
        if 0x20 <= byte < 0x7F:
            if start is None:
                start = i
        else:
            if start is not None and i - start >= min_len:
                yield start, blob[start:i].decode("ascii")
            start = None
    if start is not None and len(blob) - start >= min_len:
        yield start, blob[start:].decode("ascii")


def candidate(name: str) -> bool:
    """Identifier that looks like a Mojo method / JS binding name."""
    if not IDENT.match(name.encode()):
        return False
    if name.isupper() or name.isdigit():
        return False
    if "::" in name:
        return False
    if name.endswith(".mojom"):
        return False
    # drop Chromium's k-prefixed enum constants
    if re.match(r"^k[A-Z]", name):
        return False
    # require a camel hump so we do not collect plain words
    if not re.search(r"[a-z][A-Z]", name) and not re.search(r"[A-Z][a-z]", name[1:]):
        return False
    return True


def table_around(blob: bytes, anchor: bytes, radius: int):
    """Return identifiers within +/- radius bytes of the anchor, in order."""
    hits = []
    pos = 0
    while True:
        pos = blob.find(anchor, pos)
        if pos < 0:
            break
        hits.append(pos)
        pos += 1
    if not hits:
        return hits, []
    lo = max(0, hits[0] - radius)
    hi = min(len(blob), hits[0] + radius)
    picked = []
    for off, text in runs(blob[lo:hi], 4):
        if candidate(text):
            picked.append((off, text))
    return hits, picked


def main() -> int:
    ap = argparse.ArgumentParser()
    ap.add_argument("dll")
    ap.add_argument("--radius", type=int, default=3072)
    args = ap.parse_args()

    with open(args.dll, "rb") as fh:
        blob = fh.read()
    sections = read_sections(blob)

    tables = {
        "T1 sbipc Mojo method names (anchor: AddCanvasFingerprint)":
            b"AddCanvasFingerprint",
        "T2 renderer-side JS binding names (anchor: getCanvasFingerprintingData)":
            b"getCanvasFingerprintingData",
    }

    for title, anchor in tables.items():
        print(f"\n===== {title} =====")
        hits, picked = table_around(blob, anchor, args.radius)
        for h in hits:
            print(f"anchor at 0x{h:08x} ({section_of(sections, h)})")
        if not picked:
            print("  (no identifiers found)")
            continue
        # group into contiguous runs separated by > 8 byte gaps
        groups: list[list[tuple[int, str]]] = [[picked[0]]]
        for item in picked[1:]:
            if item[0] - groups[-1][-1][0] > 8:
                groups.append([item])
            else:
                groups[-1].append(item)
        for gi, group in enumerate(groups):
            if len(group) < 2:
                continue
            print(f"  -- group {gi} ({len(group)} names) @0x{group[0][0]:08x}")
            for off, name in group:
                print(f"     0x{off:08x}  {name}")
    return 0


if __name__ == "__main__":
    raise SystemExit(main())
