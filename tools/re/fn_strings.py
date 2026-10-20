"""List the string constants a single function references, by byte scan alone.

Why this exists
---------------
Decompiling one function in a 276 MB DLL costs a Ghidra import (minutes,
and it needs a project on disk). But the question "which string constants
does this function touch?" is answerable straight from the bytes:

    * a RIP-relative ``lea``/``mov`` against a .rdata address is the only
      way x64 code names a string;
    * the displacement is a signed 32-bit field at a fixed offset in the
      instruction, so the target can be computed without disassembling.

That gives the same "which literals are in play" answer as the decompiler,
in a fraction of a second, and it works on functions the decompiler cannot
reach in its budget.

The function range comes from the PE ``.pdata`` RUNTIME_FUNCTION table
(function boundaries, not a guess) and can be given explicitly.

Usage
-----
    python fn_strings.py <image.dll> <rva> [<rva> ...] [--len N]

``<rva>`` is the start of a function; its extent is looked up in ``.pdata``.
``--len`` bounds how many bytes are read at each referenced address.

The output lists, per function, the referenced addresses together with the
bytes they point at - which is what makes a payload schema readable.
"""

from __future__ import annotations

import argparse
import struct
import sys
from pathlib import Path

# x64 instructions that carry a RIP-relative 32-bit displacement.
# The displacement sits at the end of the instruction, so its offset from
# the start of the instruction depends on the opcode length.
#
# The value is the number of bytes from the *opcode* byte to the
# displacement field, i.e. opcode(1) + modrm(1) + optional SIB/imm.
# REX prefixes sit before the opcode and are skipped separately, so they
# do not change this number.
REL32_OPS = {
    0x8D: 2,  # lea r, [rip+disp32]
    0x8B: 2,  # mov r, [rip+disp32]
    0x89: 2,  # mov [rip+disp32], r
    0xC7: 2,  # mov dword [rip+disp32], imm32
}

# Prefixes that shift the opcode but keep the "displacement at end" shape.
REX = {0x40, 0x41, 0x42, 0x43, 0x44, 0x45, 0x46, 0x47,
       0x48, 0x49, 0x4A, 0x4B, 0x4C, 0x4D, 0x4E, 0x4F}
LEGACY_PREFIX = {0x66, 0x67, 0xF2, 0xF3}


def read_sections(blob: bytes):
    pe_off = struct.unpack_from("<I", blob, 0x3C)[0]
    if blob[pe_off : pe_off + 4] != b"PE\x00\x00":
        raise ValueError("not a PE image")
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


def read_pdata(blob: bytes, image_base: int, sections):
    """Return the list of (begin_rva, end_rva) from .pdata."""
    for name, vaddr, vsize, rawptr, rawsize in sections:
        if name == ".pdata":
            break
    else:
        return []
    count = rawsize // 12
    out = []
    for i in range(count):
        begin, end, _ = struct.unpack_from("<III", blob, rawptr + i * 12)
        out.append((begin, end))
    return out


def function_extent(pdata, rva: int):
    for begin, end in pdata:
        if begin <= rva < end:
            return begin, end
    return None


def scan_rel32(code: bytes, code_rva: int, image_base: int):
    """Yield (instruction_rva, target_va) for RIP-relative references."""
    i = 0
    n = len(code)
    while i < n:
        start = i
        # Skip prefixes (REX and legacy) - they precede the opcode.
        while i < n and (code[i] in REX or code[i] in LEGACY_PREFIX):
            i += 1
        if i >= n:
            break
        op = code[i]
        if op not in REL32_OPS:
            i += 1
            continue
        disp_at = i + REL32_OPS[op]
        if disp_at + 4 > n:
            break
        disp = struct.unpack_from("<i", code, disp_at)[0]
        # RIP at the time of the access points past the whole instruction.
        next_rva = code_rva + disp_at + 4
        yield code_rva + start, image_base + next_rva + disp
        i = disp_at + 4


def preview(data: bytes, at: int, count: int) -> str:
    chunk = data[at : at + count]
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
    ap = argparse.ArgumentParser(description="list strings a function references")
    ap.add_argument("image")
    ap.add_argument("rvas", nargs="+", help="function start RVA(s)")
    ap.add_argument("--len", type=int, default=48, help="bytes to read at each target")
    args = ap.parse_args()

    blob = Path(args.image).read_bytes()
    image_base, sections = read_sections(blob)
    pdata = read_pdata(blob, image_base, sections)
    print("image base: 0x%x   .pdata entries: %d" % (image_base, len(pdata)))

    for raw in args.rvas:
        rva = int(raw, 0)
        extent = function_extent(pdata, rva)
        if extent is None:
            print("\n# RVA 0x%x: not found in .pdata" % rva)
            continue
        begin, end = extent
        code_off = va_to_offset(image_base + begin, image_base, sections)
        if code_off is None:
            print("\n# RVA 0x%x: code not mapped" % rva)
            continue
        code = blob[code_off : code_off + (end - begin)]
        print("\n# RVA 0x%x..0x%x  (%d bytes)" % (begin, end, end - begin))
        seen = set()
        for insn_rva, target in scan_rel32(code, begin, image_base):
            tgt_off = va_to_offset(target, image_base, sections)
            if tgt_off is None or target in seen:
                continue
            seen.add(target)
            text = preview(blob, tgt_off, args.len)
            if not text:
                continue
            print("  insn 0x%08x -> 0x%012x  %r" % (insn_rva, target, text))
    return 0


if __name__ == "__main__":
    raise SystemExit(main())
