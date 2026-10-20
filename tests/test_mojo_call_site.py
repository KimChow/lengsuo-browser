"""Tests for tools/re/mojo_call_site.py.

Why these exist
---------------
``mojo_call_site.py`` recovers Mojo message metadata (method id, parameter
count, payload length) by disassembling one function. Two of its steps are
arithmetic on attacker-shaped input - a PE file - and both fail silently when
wrong:

1. **RVA -> file offset.** A wrong section pick reads the wrong bytes, which
   still disassembles into *something*. The output looks plausible and is
   fiction.
2. **Function extent.** The range comes from ``.pdata``; picking the wrong
   entry truncates or over-runs the function, dropping or inventing
   immediates.

The third thing worth pinning is the Mojo flag decode, because the low three
bits of a method id are easy to mislabel and the label then travels into
documentation.

These tests build a tiny synthetic PE in memory: no Chromium checkout, no
capstone needed for the PE/flag parts.

Usage
-----
    python -m unittest tests.test_mojo_call_site
"""

from __future__ import annotations

import importlib.util
import pathlib
import struct
import unittest

REPO = pathlib.Path(__file__).resolve().parents[1]
MODULE_PATH = REPO / "tools" / "re" / "mojo_call_site.py"

IMAGE_BASE = 0x180000000
SECTION_ALIGN = 0x1000
TEXT_RVA = 0x1000
RDATA_RVA = 0x2000
PDATA_RVA = 0x3000


def load_module():
    """Import tools/re/mojo_call_site.py by path (it is not a package)."""
    spec = importlib.util.spec_from_file_location("mojo_call_site_under_test", MODULE_PATH)
    module = importlib.util.module_from_spec(spec)
    spec.loader.exec_module(module)
    return module


try:
    mcs = load_module()
    IMPORT_ERROR = None
except SystemExit as exc:  # capstone missing: the module exits on import
    mcs = None
    IMPORT_ERROR = str(exc)


def build_pe(*, text: bytes, rdata: bytes = b"", pdata: bytes = b"") -> bytes:
    """Lay out a minimal PE32+ whose .text/.rdata/.pdata we control.

    Only the fields the tool reads are meaningful: section table, image base,
    and the three sections' bounds.
    """
    sections = [
        (".text", TEXT_RVA, text),
        (".rdata", RDATA_RVA, rdata),
        (".pdata", PDATA_RVA, pdata),
    ]

    header_size = 0x200
    body = bytearray()
    raw_ptrs = []
    for _name, rva, data in sections:
        # File offset = rva (headers are 0x200, section virtual starts are
        # section-aligned; keeping offset == rva makes assertions readable).
        raw_ptrs.append(len(body) + header_size)
        body += data
        if len(body) % SECTION_ALIGN:
            body += b"\x00" * (SECTION_ALIGN - len(body) % SECTION_ALIGN)

    opt_size = 0xF0
    coff = 4
    sec_off = coff + 20 + opt_size

    blob = bytearray(header_size)
    struct.pack_into("<I", blob, 0x3C, 0x80)  # e_lfanew
    struct.pack_into("<4s", blob, 0x80, b"PE\x00\x00")
    struct.pack_into("<H", blob, 0x80 + coff + 2, len(sections))  # NumberOfSections
    struct.pack_into("<H", blob, 0x80 + coff + 16, opt_size)  # SizeOfOptionalHeader
    struct.pack_into("<Q", blob, 0x80 + coff + 20 + 24, IMAGE_BASE)  # ImageBase

    for index, ((name, rva, data), raw_ptr) in enumerate(zip(sections, raw_ptrs)):
        base = 0x80 + sec_off + index * 40
        struct.pack_into("<8s", blob, base, name.encode("ascii"))
        struct.pack_into("<IIII", blob, base + 8, len(data), rva, len(data), raw_ptr)

    return bytes(blob) + bytes(body)


def pdata_entry(begin: int, end: int) -> bytes:
    return struct.pack("<III", begin, end, 0)


class FlagTests(unittest.TestCase):
    """Mojo message flags live in the low three bits of the ordinal."""

    def test_expects_response_bit(self) -> None:
        self.assertEqual(mcs.flags_of(0x7C97B271), ["kMessageExpectsResponse"])

    def test_is_response_and_expects_response(self) -> None:
        self.assertEqual(
            mcs.flags_of(0x39EBB1FB),
            ["kMessageExpectsResponse", "kMessageIsResponse"],
        )

    def test_no_flags(self) -> None:
        self.assertEqual(mcs.flags_of(0x12345678), [])


class PeTests(unittest.TestCase):
    """Section lookup and .pdata lookup must be exact."""

    def test_offset_uses_the_containing_section(self) -> None:
        blob = build_pe(text=b"\x90" * 16, rdata=b"hello\x00")
        pe = mcs.Pe(blob)
        # .rdata body starts right after .text's padded slot.
        expected = pe.offset(RDATA_RVA)
        self.assertEqual(pe.blob[expected : expected + 5], b"hello")

    def test_offset_outside_every_section_raises(self) -> None:
        pe = mcs.Pe(build_pe(text=b"\x90" * 16))
        with self.assertRaises(ValueError):
            pe.offset(0x9000)

    def test_section_name(self) -> None:
        pe = mcs.Pe(build_pe(text=b"\x90" * 16, rdata=b"x\x00"))
        self.assertEqual(pe.section_name(TEXT_RVA), ".text")
        self.assertEqual(pe.section_name(RDATA_RVA), ".rdata")
        self.assertEqual(pe.section_name(0x9000), "?")

    def test_cstring_reads_until_nul(self) -> None:
        pe = mcs.Pe(build_pe(text=b"\x90" * 16, rdata=b"GetInitJson\x00tail"))
        self.assertEqual(pe.cstring(RDATA_RVA), "GetInitJson")

    def test_cstring_refuses_non_ascii(self) -> None:
        """Binary data must not be reported as a string."""
        pe = mcs.Pe(build_pe(text=b"\x90" * 16, rdata=b"\x01\x02\x03\x04\x05\x06\x07"))
        self.assertIsNone(pe.cstring(RDATA_RVA))

    def test_function_range_comes_from_pdata(self) -> None:
        pdata = pdata_entry(TEXT_RVA, TEXT_RVA + 0x20) + pdata_entry(
            TEXT_RVA + 0x40, TEXT_RVA + 0x60
        )
        pe = mcs.Pe(build_pe(text=b"\x90" * 0x80, pdata=pdata))
        self.assertEqual(pe.function_range(TEXT_RVA), (TEXT_RVA, TEXT_RVA + 0x20))
        self.assertEqual(
            pe.function_range(TEXT_RVA + 0x40), (TEXT_RVA + 0x40, TEXT_RVA + 0x60)
        )

    def test_unknown_function_has_no_range(self) -> None:
        """An address between entries must not borrow a neighbour's extent."""
        pdata = pdata_entry(TEXT_RVA, TEXT_RVA + 0x20)
        pe = mcs.Pe(build_pe(text=b"\x90" * 0x80, pdata=pdata))
        self.assertIsNone(pe.function_range(TEXT_RVA + 0x40))


@unittest.skipIf(mcs is None, f"capstone unavailable: {IMPORT_ERROR}")
class DecodeTests(unittest.TestCase):
    """End-to-end: immediates and call targets from real instruction bytes."""

    # mov edx, 0x39EBB1FB ; mov r8d, 5 ; mov edx, 0x10 ; ret
    CODE = (
        b"\xba\xfb\xb1\xeb\x39"
        b"\x41\xb8\x05\x00\x00\x00"
        b"\xba\x10\x00\x00\x00"
        b"\xc3"
    )

    def _decode(self, code: bytes, size: int | None = None) -> dict:
        size = len(code) if size is None else size
        pdata = pdata_entry(TEXT_RVA, TEXT_RVA + size)
        pe = mcs.Pe(build_pe(text=code, pdata=pdata))
        return mcs.decode(pe, TEXT_RVA)

    def test_method_id_is_reported_with_flags(self) -> None:
        result = self._decode(self.CODE)
        ids = [i for i in result["immediates"] if i["value"] == 0x39EBB1FB]
        self.assertEqual(len(ids), 1, "method id must be found exactly once")
        self.assertEqual(ids[0]["register"], "edx")
        self.assertEqual(
            ids[0]["mojo_flags"],
            ["kMessageExpectsResponse", "kMessageIsResponse"],
        )

    def test_parameter_count_is_reported(self) -> None:
        result = self._decode(self.CODE)
        counts = [i for i in result["immediates"] if i["register"] == "r8d"]
        self.assertEqual(len(counts), 1)
        self.assertEqual(counts[0]["value"], 5)

    def test_payload_length_is_reported(self) -> None:
        result = self._decode(self.CODE)
        sixteens = [i for i in result["immediates"] if i["value"] == 0x10]
        self.assertEqual(len(sixteens), 1)
        self.assertEqual(sixteens[0]["register"], "edx")

    def test_plain_numeric_immediates_get_no_flags(self) -> None:
        """A count or a byte length must never be labelled a Mojo ordinal."""
        result = self._decode(self.CODE)
        for imm in result["immediates"]:
            if imm["value"] != 0x39EBB1FB:
                self.assertNotIn("mojo_flags", imm)

    def test_truncated_function_does_not_raise(self) -> None:
        """A function cut mid-instruction must yield the complete prefix, not crash."""
        # Dropping the last 2 bytes cuts into the third instruction, so exactly
        # the first two survive; the partial one is not reported as decoded.
        result = self._decode(self.CODE, size=len(self.CODE) - 2)
        self.assertEqual(len(result["instructions"]), 2)
        self.assertEqual(result["instructions"][0]["text"], "mov edx, 0x39ebb1fb")

    def test_direct_call_target_is_resolved_to_rva(self) -> None:
        # call rel32 -> target; ret
        target_rva = TEXT_RVA + 0x40
        call_at = TEXT_RVA
        rel = target_rva - (call_at + 5)
        code = b"\xe8" + struct.pack("<i", rel) + b"\xc3"
        pdata = pdata_entry(TEXT_RVA, TEXT_RVA + len(code)) + pdata_entry(
            target_rva, target_rva + 1
        )
        pe = mcs.Pe(build_pe(text=code.ljust(0x50, b"\x90"), pdata=pdata))
        result = mcs.decode(pe, TEXT_RVA)
        calls = [row["callee_rva"] for row in result["instructions"] if "callee_rva" in row]
        self.assertEqual(calls, [target_rva])

    def test_rip_string_target_is_resolved(self) -> None:
        # lea rax, [rip+disp] pointing at .rdata holding a cstring.
        text_size = 0x40
        insn_at = TEXT_RVA
        insn_len = 7
        disp = (RDATA_RVA) - (insn_at + insn_len)
        code = b"\x48\x8d\x05" + struct.pack("<i", disp) + b"\xc3"
        pdata = pdata_entry(TEXT_RVA, TEXT_RVA + len(code))
        pe = mcs.Pe(
            build_pe(
                text=code.ljust(text_size, b"\x90"),
                rdata=b"sbipcRendererClient::x\x00",
                pdata=pdata,
            )
        )
        result = mcs.decode(pe, TEXT_RVA)
        found = [row for row in result["instructions"] if "rip_target_string" in row]
        self.assertEqual(len(found), 1)
        self.assertEqual(found[0]["rip_target_string"], "sbipcRendererClient::x")

    def test_boundary_is_reported_as_pdata_when_known(self) -> None:
        self.assertEqual(self._decode(self.CODE)["boundary"], ".pdata")

    def test_boundary_falls_back_when_pdata_lacks_the_entry(self) -> None:
        """No .pdata entry => a window, and the caller must be able to tell."""
        pe = mcs.Pe(build_pe(text=self.CODE.ljust(0x300, b"\x90")))
        result = mcs.decode(pe, TEXT_RVA)
        self.assertEqual(result["boundary"], "window")


if __name__ == "__main__":
    unittest.main()
