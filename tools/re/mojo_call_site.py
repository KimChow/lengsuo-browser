"""Read a native function's immediate constants, to recover Mojo message metadata.

Why this exists
---------------
For a generated Mojo call site, three facts are compile-time constants and are
written straight into the instruction stream:

    * the method id            ``mov edx, <imm32>``
    * the parameter count      ``mov r8d, <imm>``
    * the parameter byte size  ``mov edx, <imm>`` right before the serializer

None of them needs a symbol, a PDB, or a decompiler. A disassembler pass over
one function's bytes reads them out exactly. This is the cheap answer to
"what does this interface method take and return?" when the provider cannot
hold a whole 276 MB image in its analysis budget.

Unlike ``fn_strings.py`` (which only resolves RIP-relative data references),
this tool also resolves direct ``call rel32`` / ``jmp rel32`` targets to RVAs,
because the interesting ones here are calls to the payload allocator and the
argument serializer.

Usage
-----
    python mojo_call_site.py <image.dll> <rva> [<rva> ...] [--json]

``<rva>`` is the start of a function. Its extent comes from the PE ``.pdata``
RUNTIME_FUNCTION table, so the boundary is looked up rather than guessed.

Exit status is 0 when every requested function was decoded, 2 on bad input.
"""

from __future__ import annotations

import argparse
import json
import struct
import sys

try:
    import capstone
except ImportError:  # pragma: no cover - dependency hint is the whole point
    sys.exit("capstone is required: pip install capstone")

# x64 register ids we care about when reading an immediate.
#   edx / r8d are where System V / Windows x64 put the 2nd and 3rd arguments
#   of the runtime helpers Mojo generates.
_ARG_REGS = {
    capstone.x86.X86_REG_EDX: "arg1/edx",
    capstone.x86.X86_REG_R8D: "arg2/r8d",
    capstone.x86.X86_REG_ECX: "arg0/ecx",
}

# Mojo message flags live in the low three bits of the ordinal.
_MOJO_FLAGS = {
    0x1: "kMessageExpectsResponse",
    0x2: "kMessageIsResponse",
    0x4: "kMessageIsUrgent",
}


class Pe:
    """Just enough PE parsing to map RVA <-> file offset and read .pdata."""

    def __init__(self, blob: bytes) -> None:
        self.blob = blob
        pe = struct.unpack_from("<I", blob, 0x3C)[0]
        coff = pe + 4
        nsec = struct.unpack_from("<H", blob, coff + 2)[0]
        opt_size = struct.unpack_from("<H", blob, coff + 16)[0]
        self.image_base = struct.unpack_from("<Q", blob, coff + 20 + 24)[0]
        sec_off = coff + 20 + opt_size
        self.sections = []
        for i in range(nsec):
            base = sec_off + i * 40
            name = blob[base : base + 8].rstrip(b"\x00").decode("ascii", "replace")
            vsize, vaddr, rawsize, rawptr = struct.unpack_from("<IIII", blob, base + 8)
            self.sections.append((name, vaddr, max(vsize, rawsize), rawptr))

    def offset(self, rva: int) -> int:
        for _name, vaddr, vsize, rawptr in self.sections:
            if vaddr <= rva < vaddr + vsize:
                return rawptr + (rva - vaddr)
        raise ValueError(f"rva {rva:#x} is not inside any section")

    def section_name(self, rva: int) -> str:
        for name, vaddr, vsize, _raw in self.sections:
            if vaddr <= rva < vaddr + vsize:
                return name
        return "?"

    def cstring(self, rva: int, limit: int = 100) -> str | None:
        try:
            off = self.offset(rva)
        except ValueError:
            return None
        raw = self.blob[off : off + limit].split(b"\x00")[0]
        if len(raw) >= 6 and all(0x20 <= c < 0x7F for c in raw):
            return raw.decode("ascii", "replace")
        return None

    def function_range(self, rva: int) -> tuple[int, int] | None:
        """Look the enclosing function up in .pdata (RUNTIME_FUNCTION, 12 B each)."""
        for name, vaddr, vsize, rawptr in self.sections:
            if name != ".pdata":
                continue
            for i in range(vsize // 12):
                begin, end, _unwind = struct.unpack_from("<III", self.blob, rawptr + i * 12)
                if begin == rva:
                    return begin, end
        return None


def flags_of(ordinal: int) -> list[str]:
    return [name for bit, name in _MOJO_FLAGS.items() if ordinal & bit]


def decode(pe: Pe, rva: int) -> dict:
    rng = pe.function_range(rva)
    if rng is None:
        # Fall back to a fixed window so a non-.pdata entry still yields output.
        begin, end = rva, rva + 0x300
        boundary = "window"
    else:
        begin, end = rng
        boundary = ".pdata"

    code = pe.blob[pe.offset(begin) : pe.offset(begin) + (end - begin)]
    md = capstone.Cs(capstone.CS_ARCH_X86, capstone.CS_MODE_64)
    md.detail = True

    insns: list[dict] = []
    immediates: list[dict] = []
    calls: set[int] = set()
    strings: list[dict] = []

    for ins in md.disasm(code, pe.image_base + begin):
        row: dict = {
            "rva": ins.address - pe.image_base,
            "text": f"{ins.mnemonic} {ins.op_str}".strip(),
        }
        # RIP-relative data reference -> try to name the bytes it points at.
        for op in ins.operands:
            if op.type == capstone.x86.X86_OP_MEM and op.mem.base == capstone.x86.X86_REG_RIP:
                target = ins.address + ins.size + op.mem.disp
                trva = target - pe.image_base
                row["rip_target_rva"] = trva
                text = pe.cstring(trva)
                if text:
                    row["rip_target_string"] = text
                    strings.append({"from_rva": row["rva"], "rva": trva, "text": text})
        # Direct near call/jmp -> record the callee RVA (no Ghidra needed).
        if ins.mnemonic in ("call", "jmp"):
            for op in ins.operands:
                if op.type == capstone.x86.X86_OP_IMM:
                    callee = op.imm - pe.image_base
                    row["callee_rva"] = callee
                    calls.add(callee)
        # mov <reg32>, imm  -> a candidate encoded constant.
        if ins.mnemonic == "mov" and len(ins.operands) == 2:
            dst, src = ins.operands
            if (
                dst.type == capstone.x86.X86_OP_REG
                and src.type == capstone.x86.X86_OP_IMM
                and dst.reg in _ARG_REGS
            ):
                imm = src.imm & 0xFFFFFFFF
                entry = {
                    "rva": row["rva"],
                    "register": ins.reg_name(dst.reg),
                    "slot": _ARG_REGS[dst.reg],
                    "value": imm,
                    "hex": f"{imm:#010x}",
                }
                if dst.reg == capstone.x86.X86_REG_EDX and imm & 0x7:
                    entry["mojo_flags"] = flags_of(imm)
                immediates.append(entry)
        insns.append(row)

    return {
        "rva": begin,
        "end_rva": end,
        "size": end - begin,
        "boundary": boundary,
        "instructions": insns,
        "immediates": immediates,
        "callees": sorted(calls),
        "strings": strings,
    }


def render(result: dict) -> str:
    out = [
        f"# RVA {result['rva']:#010x}..{result['end_rva']:#010x}"
        f"  ({result['size']} bytes, boundary={result['boundary']})",
        "",
        "## immediates (candidate Mojo metadata)",
    ]
    if not result["immediates"]:
        out.append("  (none)")
    for imm in result["immediates"]:
        flags = imm.get("mojo_flags")
        tail = f"   flags: {', '.join(flags)}" if flags else ""
        out.append(f"  +{imm['rva'] - result['rva']:#05x}  mov {imm['register']}, {imm['hex']}{tail}")
    out.append("")
    out.append("## callees")
    for callee in result["callees"]:
        out.append(f"  {callee:#010x}")
    out.append("")
    out.append("## instructions")
    for row in result["instructions"]:
        extra = ""
        if "rip_target_string" in row:
            extra = f'   -> "{row["rip_target_string"]}"'
        elif "callee_rva" in row:
            extra = f"   -> {row['callee_rva']:#010x}"
        out.append(f"  {row['rva']:#010x}  {row['text']}{extra}")
    return "\n".join(out)


def main(argv: list[str]) -> int:
    ap = argparse.ArgumentParser(description=__doc__)
    ap.add_argument("image")
    ap.add_argument("rvas", nargs="+", help="function start RVAs, e.g. 0x030FA6F0")
    ap.add_argument("--json", action="store_true", help="emit JSON instead of text")
    args = ap.parse_args(argv)

    with open(args.image, "rb") as handle:
        pe = Pe(handle.read())

    payload = []
    for raw in args.rvas:
        rva = int(raw, 0)
        result = decode(pe, rva)
        payload.append(result)
        if not args.json:
            print(render(result))
            print()

    if args.json:
        json.dump(payload, sys.stdout, indent=2)
        print()
    return 0


if __name__ == "__main__":
    raise SystemExit(main(sys.argv[1:]))
