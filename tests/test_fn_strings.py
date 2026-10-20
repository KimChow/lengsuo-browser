"""Tests for tools/re/fn_strings.py.

Why these exist
---------------
``fn_strings.py`` answers "which string constants does this function use?"
without a decompiler, by decoding RIP-relative references straight from the
instruction bytes. That only works if the displacement arithmetic is exact.

The arithmetic has two easy-to-get-wrong parts, and both were wrong in the
first cut:

1. **Where the displacement sits.** It is the last field of the
   instruction, so its offset depends on the instruction's own length - not
   on a constant "opcode plus something". A REX prefix shifts the opcode but
   not the displacement's distance from the *opcode*.
2. **What RIP points at.** x64 RIP-relative addressing is measured from the
   *end* of the instruction, not from the byte after the displacement.

A one-off error in either direction yields plausible-looking but wrong
addresses, which then decode as garbage bytes - the failure does not look
like a failure. These tests pin the arithmetic so it fails loudly instead.

Usage
-----
    python -m unittest tests.test_fn_strings
"""

from __future__ import annotations

import importlib.util
import pathlib
import struct
import unittest

REPO = pathlib.Path(__file__).resolve().parents[1]
MODULE_PATH = REPO / "tools" / "re" / "fn_strings.py"


def load_module():
    """Import tools/re/fn_strings.py by path (it is not a package)."""
    spec = importlib.util.spec_from_file_location("fn_strings_under_test", MODULE_PATH)
    module = importlib.util.module_from_spec(spec)
    spec.loader.exec_module(module)
    return module


fn = load_module()


def lea_rip(reg: int, disp: int) -> bytes:
    """Encode ``lea reg, [rip+disp]`` as REX.W + 8D /r + disp32."""
    modrm = 0x05 | ((reg & 7) << 3)  # mod=00, rm=101 => RIP-relative
    return b"\x48\x8d" + bytes([modrm]) + struct.pack("<i", disp)


class Rel32ScanTests(unittest.TestCase):
    """The displacement arithmetic must be exact."""

    def test_lea_resolves_to_the_next_instruction_relative_target(self) -> None:
        """A RIP-relative lea targets base + insn_end + disp."""
        image_base = 0x180000000
        code_rva = 0x1000
        disp = 0x200
        code = lea_rip(0, disp)
        hits = list(fn.scan_rel32(code, code_rva, image_base))
        self.assertEqual(len(hits), 1, "expected exactly one reference")
        insn_rva, target = hits[0]
        self.assertEqual(insn_rva, code_rva)
        # lea is 7 bytes: REX + 8D + modrm + disp32.
        expected = image_base + code_rva + len(code) + disp
        self.assertEqual(
            target,
            expected,
            "target must be measured from the end of the instruction",
        )

    def test_rex_prefix_does_not_shift_the_displacement(self) -> None:
        """Adding a REX prefix moves the opcode but not disp-from-opcode."""
        image_base = 0x180000000
        code_rva = 0x2000
        disp = -0x40

        rex = lea_rip(0, disp)
        # Same instruction without REX.W: 8D + modrm + disp32.
        no_rex = b"\x8d" + rex[2:]

        rex_hits = list(fn.scan_rel32(rex, code_rva, image_base))
        plain_hits = list(fn.scan_rel32(no_rex, code_rva, image_base))

        self.assertEqual(len(rex_hits), 1)
        self.assertEqual(len(plain_hits), 1)

        # Both name the same target: the encoded displacement is identical,
        # and because REX is skipped as a prefix the instruction start is
        # the same, so insn_end differs only by the prefix length. The
        # targets therefore differ by exactly that prefix length - which is
        # why the prefix must be counted as part of the instruction.
        rex_target = rex_hits[0][1]
        plain_target = plain_hits[0][1]
        self.assertEqual(
            rex_target - plain_target,
            1,
            "REX prefix must be counted in the instruction length",
        )

    def test_negative_displacement_points_backwards(self) -> None:
        """A backwards reference must not be treated as unsigned."""
        image_base = 0x180000000
        code_rva = 0x3000
        disp = -0x1000
        code = lea_rip(3, disp)
        hits = list(fn.scan_rel32(code, code_rva, image_base))
        self.assertEqual(len(hits), 1)
        _insn, target = hits[0]
        self.assertEqual(target, image_base + code_rva + len(code) + disp)
        self.assertLess(target, image_base + code_rva)

    def test_non_relative_opcodes_are_ignored(self) -> None:
        """Ordinary instructions must not produce spurious references."""
        code = b"\x90" * 16  # nop slide
        hits = list(fn.scan_rel32(code, 0x4000, 0x180000000))
        self.assertEqual(hits, [])

    def test_truncated_instruction_does_not_raise(self) -> None:
        """A trailing partial lea must be skipped, not crash the scan."""
        code = b"\x48\x8d\x05\x11"  # disp32 cut short
        hits = list(fn.scan_rel32(code, 0x5000, 0x180000000))
        self.assertEqual(hits, [])


class PreviewTests(unittest.TestCase):
    """Byte-to-text rendering must stop at NUL and escape the rest."""

    def test_stops_at_nul(self) -> None:
        self.assertEqual(fn.preview(b"from\x00garbage", 0, 32), "from")

    def test_escapes_non_printable(self) -> None:
        self.assertEqual(fn.preview(b"\x01\x02", 0, 2), "\\x01\\x02")

    def test_empty_when_first_byte_is_nul(self) -> None:
        self.assertEqual(fn.preview(b"\x00abc", 0, 8), "")


class PeParsingTests(unittest.TestCase):
    """Section lookup is what turns an address into file bytes."""

    def test_va_to_offset_uses_the_containing_section(self) -> None:
        sections = [
            (".text", 0x1000, 0x1000, 0x400, 0x1000),
            (".rdata", 0x2000, 0x1000, 0x1400, 0x1000),
        ]
        image_base = 0x180000000
        self.assertEqual(
            fn.va_to_offset(image_base + 0x2100, image_base, sections), 0x1500
        )

    def test_va_outside_any_section_is_none(self) -> None:
        sections = [(".text", 0x1000, 0x100, 0x400, 0x100)]
        self.assertIsNone(fn.va_to_offset(0x180000000 + 0x5000, 0x180000000, sections))


if __name__ == "__main__":
    unittest.main()
