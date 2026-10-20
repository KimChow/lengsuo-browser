"""Resolve PE virtual addresses to the strings they point at.

Why this exists
---------------
The fixed-point Ghidra decompiler (``tools/re/ghidra/DecompileAtRva.java``)
prints string references as raw addresses (``&UNK_18f1fdc3a``) because the
image is imported with auto-analysis disabled: there are no symbols, so Ghidra
cannot label the pointer.

For the fingerprint channel that is the missing half of the answer. The
browser-side producer turns out to be a **key/value message builder**: every
payload entry is emitted as ``(key address, key length)`` followed by a typed
setter. Knowing the *keys* is what turns "it builds a message" into "it
builds a message with these named fields".

This tool takes the addresses Ghidra printed and prints the bytes they point
at, so the payload schema can be read off the binary without paying for a
full disassembly pass.

Usage
-----
    python pe_strings.py <image.dll> <va> [<va> ...] [--len N]

``<va>`` is a full virtual address (0x1831fdc3a), matching what Ghidra
prints. ``--len`` forces an exact byte count instead of reading a
NUL-terminated string (Ghidra often prints an explicit length, which is more
reliable than trusting the byte after the string).

The address arithmetic is the standard PE one: the RVA is located inside a
section's virtual range, and the file offset is ``rawptr + (rva - vaddr)``.
"""

from __future__ import annotations

import argparse
import struct
import sys
from pathlib import Path

DEFAULT_LENGTH = 64


def read_sections(blob: bytes) -> tuple[int, list[tuple[str, int, int, int, int]]]:
    """Return (image_base, [(name, vaddr, vsize, rawptr, rawsize), ...])."""
    pe_off = struct.unpack_from("<I", blob, 0x3C)[0]
    if blob[pe_off : pe_off + 4] != b"PE\x00\x00":
        raise ValueError("not a PE image (bad PE signature)")
    coff = pe_off + 4
    num_sections = struct.unpack_from("<H", blob, coff + 2)[0]
    opt_size = struct.unpack_from("<H", blob, coff + 16)[0]
    opt = coff + 20
    image_base = struct.unpack_from("<Q", blob, opt + 24)[0]
    sections = []
    base = opt + opt_size
    for i in range(num_sections):
        off = base + i * 40
        name = blob[off : off + 8].split(b"\x00")[0].decode("ascii", "replace")
        vsize, vaddr, rawsize, rawptr = struct.unpack_from("<IIII", blob, off + 8)
        sections.append((name, vaddr, vsize, rawptr, rawsize))
    return image_base, sections


def va_to_offset(va: int, image_base: int, sections) -> int | None:
    rva = va - image_base
    for _name, vaddr, vsize, rawptr, rawsize in sections:
        if vaddr <= rva < vaddr + max(vsize, rawsize):
            return rawptr + (rva - vaddr)
    return None


def preview(data: bytes, at: int, count: int) -> bytes:
    chunk = data[at : at + count]
    return chunk


def printable(chunk: bytes) -> str:
    out = []
    for b in chunk:
        if 0x20 <= b < 0x7F:
            out.append(chr(b))
        elif b == 0:
            break
        else:
            out.append("\\x%02x" % b)
    return "".join(out)


def main() -> int:
    ap = argparse.ArgumentParser(description="resolve PE VAs to strings")
    ap.add_argument("image")
    ap.add_argument("vas", nargs="+", help="virtual addresses, e.g. 0x1831fdc3a")
    ap.add_argument("--len", type=int, default=0, help="exact byte count to read")
    args = ap.parse_args()

    blob = Path(args.image).read_bytes()
    image_base, sections = read_sections(blob)
    print("image base: 0x%x  sections: %d" % (image_base, len(sections)))
    print()

    for raw in args.vas:
        va = int(raw, 0)
        off = va_to_offset(va, image_base, sections)
        if off is None:
            print("%-14s  <unmapped>" % raw)
            continue
        count = args.len if args.len else DEFAULT_LENGTH
        chunk = preview(blob, off, count)
        text = printable(chunk)
        hexs = chunk[:32].hex(" ")
        print("%-14s  fileoff=0x%-9x  %r" % (raw, off, text))
        print("%-14s  hex: %s" % ("", hexs))
    return 0


if __name__ == "__main__":
    raise SystemExit(main())
