"""Locate the code that implements the the reference Mojo fingerprint methods.

Why this exists
---------------
The previous scans (mojo_interface_scan.py, mojo_surface_scan.py) proved that
the interface surface exists as plain strings in chrome.dll:

    sbipcRendererClient::getCanvasFingerprintingData
    sbipcRendererClient::getWebGLFingerprintingData

but strings prove names, not code. To learn *how* the fingerprint is handed to
the renderer we need the function that references those names.

Running Ghidra over the whole 276 MB DLL blows the 330 s startup budget, so the
strategy is:

    1. find the string in the file                  (byte scan)
    2. convert the file offset to a virtual address (PE section table)
    3. find the code that references that address   (RIP-relative scan)
    4. guess the enclosing function entry           (padding-run heuristic)

Step 4 is a heuristic, not proof: the caller is a candidate for Ghidra to
confirm or reject. Everything printed here is derived from the shipped bytes.

Usage
-----
    python mojo_xref_scan.py <chrome.dll> [--needles A,B,C] [--text-only]
"""

from __future__ import annotations

import argparse
import re
import struct
import sys
from dataclasses import dataclass

DEFAULT_NEEDLES = [
    "sbipcRendererClient::getCanvasFingerprintingData",
    "sbipcRendererClient::getWebGLFingerprintingData",
    "getCanvasFingerprintingData",
    "getWebGLFingerprintingData",
    "superBrowser.mojom.sbipcRendererClient",
]

# REX.W prefixed LEA/MOV with a RIP-relative ModRM (mod=00, rm=101).
#   48 8D 0D disp32  ->  lea rcx, [rip+disp32]
#   4C 8D 05 disp32  ->  lea r8,  [rip+disp32]
#   48 8B 15 disp32  ->  mov rdx, [rip+disp32]
_REX_PREFIXED = re.compile(
    rb"(?:\x48|\x4C)[\x8D\x8B][\x05\x0D\x15\x1D\x25\x2D\x35\x3D](.{4})",
    re.DOTALL,
)

# 32-bit forms without REX.W, still used for absolute addresses in 64-bit code.
#   8D 0D disp32 -> lea ecx, [rip+disp32]
_PLAIN = re.compile(rb"[\x8D\x8B][\x05\x0D\x15\x1D\x25\x2D\x35\x3D](.{4})", re.DOTALL)

# Padding bytes Ghidra/Clang emit between functions.
_PADDING = b"\xcc\x90\x00"


@dataclass
class Section:
    name: str
    vaddr: int
    vsize: int
    rawptr: int
    rawsize: int

    @property
    def end(self) -> int:
        return self.rawptr + self.rawsize


@dataclass
class Hit:
    """One RIP-relative reference from code to a target address."""

    site_file_offset: int
    site_va: int
    target_va: int
    kind: str


def read_pe(blob: bytes) -> tuple[int, list[Section]]:
    """Return (image_base, sections) for a PE image."""
    if blob[:2] != b"MZ":
        raise ValueError("not a PE file (no MZ header)")
    pe_off = struct.unpack_from("<I", blob, 0x3C)[0]
    if blob[pe_off : pe_off + 4] != b"PE\x00\x00":
        raise ValueError("not a PE file (no PE signature)")
    coff = pe_off + 4
    machine, num_sections = struct.unpack_from("<HH", blob, coff)
    if machine != 0x8664:
        raise ValueError(f"expected x86-64, got machine=0x{machine:04X}")
    opt_size = struct.unpack_from("<H", blob, coff + 16)[0]
    opt = coff + 20
    magic = struct.unpack_from("<H", blob, opt)[0]
    if magic != 0x20B:
        raise ValueError(f"expected PE32+, got optional-header magic 0x{magic:04X}")
    image_base = struct.unpack_from("<Q", blob, opt + 24)[0]
    sec_off = opt + opt_size
    sections: list[Section] = []
    for i in range(num_sections):
        base = sec_off + i * 40
        name = blob[base : base + 8].rstrip(b"\x00").decode("ascii", "replace")
        vsize, vaddr, rawsize, rawptr = struct.unpack_from("<IIII", blob, base + 8)
        sections.append(Section(name, vaddr, vsize, rawptr, rawsize))
    return image_base, sections


def file_off_to_va(off: int, image_base: int, sections: list[Section]) -> int | None:
    for sec in sections:
        if sec.rawptr <= off < sec.end:
            return image_base + sec.vaddr + (off - sec.rawptr)
    return None


def va_to_file_off(va: int, image_base: int, sections: list[Section]) -> int | None:
    rva = va - image_base
    for sec in sections:
        if sec.vaddr <= rva < sec.vaddr + max(sec.vsize, sec.rawsize):
            return sec.rawptr + (rva - sec.vaddr)
    return None


def find_bytes(blob: bytes, needle: bytes) -> list[int]:
    """All file offsets of needle (non-overlapping is fine, needles are unique)."""
    out: list[int] = []
    start = 0
    while True:
        idx = blob.find(needle, start)
        if idx < 0:
            return out
        out.append(idx)
        start = idx + 1


def code_sections(sections: list[Section]) -> list[Section]:
    return [s for s in sections if s.name in (".text", ".text$mn", ".text$mut")]


def scan_rip_refs(
    blob: bytes,
    sec: Section,
    image_base: int,
    targets: dict[int, str],
) -> list[Hit]:
    """Find RIP-relative references from one code section to the target VAs."""
    hits: list[Hit] = []
    data = blob[sec.rawptr : sec.rawptr + sec.rawsize]
    sec_va = image_base + sec.vaddr
    for pattern, kind, extra in ((_REX_PREFIXED, "rex", 1), (_PLAIN, "plain", 0)):
        for m in pattern.finditer(data):
            disp = struct.unpack("<i", m.group(1))[0]
            instr_len = 7 if extra else 6
            site_off = sec.rawptr + m.start()
            site_va = sec_va + m.start()
            target_va = site_va + instr_len + disp
            if target_va in targets:
                hits.append(Hit(site_off, site_va, target_va, kind))
    return hits


def guess_function_entry(
    blob: bytes,
    site_off: int,
    sec: Section,
    image_base: int,
    max_back: int = 0x400,
) -> tuple[int, int]:
    """Heuristic: walk back over padding to the first aligned non-padding byte.

    Returns (file_offset, va) of the guessed entry and the offset it was
    found at. x86-64 functions in Chromium binaries are typically 16-byte
    aligned and separated by padding runs, so the byte right after the last
    padding run is the most likely entry.
    """
    lo = max(sec.rawptr, site_off - max_back)
    i = site_off
    # Walk back while the preceding bytes are padding.
    while i > lo and blob[i - 1] in _PADDING:
        i -= 1
    # Now i is the first non-padding byte after the padding run. The function
    # entry is the aligned start of the region containing it.
    entry = i
    # Refine: scan forward for a 16-byte alignment boundary that still has no
    # padding before it.
    while entry > lo and (entry % 16) != 0:
        entry -= 1
    return entry, image_base + sec.vaddr + (entry - sec.rawptr)


def parse_pdata(blob: bytes, sections: list[Section]) -> list[tuple[int, int]]:
    """Parse the x64 .pdata RUNTIME_FUNCTION table into (begin_rva, end_rva).

    This is the authoritative function-boundary table that the OS unwinder
    uses. Every function with exception-handling metadata has exactly one
    entry, so a reference site can be mapped to its enclosing function
    without guessing.
    """
    out: list[tuple[int, int]] = []
    for sec in sections:
        if sec.name != ".pdata":
            continue
        count = sec.rawsize // 12
        for i in range(count):
            base = sec.rawptr + i * 12
            begin, end, _unwind = struct.unpack_from("<III", blob, base)
            if begin == 0 and end == 0:
                continue
            out.append((begin, end))
    out.sort()
    return out


def pdata_lookup(
    pdata: list[tuple[int, int]], site_rva: int
) -> tuple[int, int] | None:
    """Find the .pdata entry whose [begin, end) range contains site_rva."""
    lo, hi = 0, len(pdata)
    while lo < hi:
        mid = (lo + hi) // 2
        begin, end = pdata[mid]
        if site_rva < begin:
            hi = mid
        elif site_rva >= end:
            lo = mid + 1
        else:
            return begin, end
    return None



def scan_pointer_tables(
    blob: bytes,
    sections: list[Section],
    image_base: int,
    targets: dict[int, str],
) -> list[tuple[int, int, int]]:
    """Find 64-bit pointers in .rdata that point at the target addresses.

    Mojo keeps a dispatch table of {method name pointer, handler pointer}
    pairs. The name pointer lands in .rdata; the handler pointer lands in
    .text. Locating the name pointer therefore finds the dispatch table, and
    the neighbouring code pointer is the handler that implements the method.
    """
    hits: list[tuple[int, int, int]] = []
    for sec in sections:
        if sec.name not in (".rdata", ".data"):
            continue
        data = blob[sec.rawptr : sec.rawptr + sec.rawsize]
        for target_va, label in targets.items():
            needle = struct.pack("<Q", target_va)
            start = 0
            while True:
                idx = data.find(needle, start)
                if idx < 0:
                    break
                slot_off = sec.rawptr + idx
                hits.append((slot_off, target_va, label))
                start = idx + 1
    return hits


def main() -> int:
    ap = argparse.ArgumentParser()
    ap.add_argument("dll")
    ap.add_argument("--needles", default=",".join(DEFAULT_NEEDLES))
    ap.add_argument("--text-only", action="store_true",
                    help="only report strings, skip the code reference scan")
    ap.add_argument("--pointers", action="store_true",
                    help="also report .rdata pointer slots that target the strings")
    args = ap.parse_args()

    with open(args.dll, "rb") as fh:
        blob = fh.read()

    image_base, sections = read_pe(blob)
    print(f"file        : {args.dll}")
    print(f"size        : {len(blob):,} bytes")
    print(f"image base  : 0x{image_base:016X}")
    print("sections    :")
    for sec in sections:
        print(f"  {sec.name:<10} rva=0x{sec.vaddr:08X} vsize=0x{sec.vsize:08X} "
              f"raw=0x{sec.rawptr:08X}+0x{sec.rawsize:08X}")
    print()

    needles = [n for n in args.needles.split(",") if n]
    targets: dict[int, str] = {}
    print("== string occurrences ==")
    for needle in needles:
        raw = needle.encode("utf-8")
        offsets = find_bytes(blob, raw)
        if not offsets:
            print(f"  [MISS] {needle}")
            continue
        for off in offsets:
            va = file_off_to_va(off, image_base, sections)
            sec_name = next(
                (s.name for s in sections if s.rawptr <= off < s.end), "?"
            )
            print(f"  [HIT ] {needle}")
            print(f"         file=0x{off:08X} va=0x{va:016X} section={sec_name}")
            if va is not None:
                targets.setdefault(va, needle)
    print()

    if args.text_only:
        if args.pointers:
            dump_pointers(blob, sections, image_base, targets)
        return 0

    print("== code references to those addresses ==")
    pdata = parse_pdata(blob, sections)
    print(f"  (.pdata entries: {len(pdata)})")
    total = 0
    for sec in code_sections(sections):
        hits = scan_rip_refs(blob, sec, image_base, targets)
        if not hits:
            continue
        print(f"-- section {sec.name} --")
        for hit in sorted(hits, key=lambda h: h.site_va):
            site_rva = hit.site_va - image_base
            entry = pdata_lookup(pdata, site_rva)
            print(f"  site va=0x{hit.site_va:016X} file=0x{hit.site_file_offset:08X} "
                  f"({hit.kind}) -> 0x{hit.target_va:016X} "
                  f"[{targets[hit.target_va]}]")
            if entry:
                begin, end = entry
                entry_va = image_base + begin
                print(f"     enclosing function (from .pdata): "
                      f"va=0x{entry_va:016X} "
                      f"rva=0x{begin:08X}..0x{end:08X} size=0x{end - begin:X}")
            else:
                entry_off, entry_va = guess_function_entry(
                    blob, hit.site_file_offset, sec, image_base
                )
                print(f"     guessed function entry (heuristic): "
                      f"va=0x{entry_va:016X} file=0x{entry_off:08X}")
            total += 1
    if total == 0:
        print("  (none)")
    print()

    if args.pointers:
        dump_pointers(blob, sections, image_base, targets)
    return 0


def dump_pointers(blob: bytes, sections: list[Section], image_base: int,
                  targets: dict[int, str]) -> None:
    print("== .rdata pointer slots targeting those strings ==")
    ptr_hits = scan_pointer_tables(blob, sections, image_base, targets)
    if not ptr_hits:
        print("  (none)")
        return
    for slot_off, target_va, label in sorted(ptr_hits):
        slot_va = file_off_to_va(slot_off, image_base, sections)
        print(f"  slot va=0x{slot_va:016X} file=0x{slot_off:08X} "
              f"-> 0x{target_va:016X} [{label}]")
        # Mojo tables pair the name pointer with a code pointer, so the
        # neighbouring slots show the dispatch entry.
        for delta in (-16, -8, 8, 16):
            nb = slot_off + delta
            if nb < 0 or nb + 8 > len(blob):
                continue
            value = struct.unpack_from("<Q", blob, nb)[0]
            nva = file_off_to_va(nb, image_base, sections)
            kind = "?"
            if image_base <= value < image_base + 0x20000000:
                kind = "code?"
            elif value < 0x1000:
                kind = "small"
            print(f"      slot{delta:+d} va=0x{nva:016X} "
                  f"value=0x{value:016X} ({kind})")


if __name__ == "__main__":
    sys.exit(main())
