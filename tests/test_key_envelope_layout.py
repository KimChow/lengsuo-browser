"""Guards the KeyEnvelope payload layout in the cross-repo wire contract.

Why this exists
---------------
The contract's fixed test vector is the only thing the kernel repo and the
client repo agree on, so every byte it states has to be internally
consistent. A past revision stated three different payload lengths in
three different places: the field table, the byte listing, and the AAD's
length field. Both sides then implemented a different length, and the
cross-repo comparison could never pass.

The GCM tag settles it. The tag is computed over the AAD, and the AAD's
first four bytes are the frame header's length field. A tag that verifies
against one length and not another therefore *proves* which payload length
the vector was built for. This test checks that agreement mechanically so
a future edit cannot reintroduce the split.

The test derives the layout from the document; it does not hard-code the
expected length, so a deliberate protocol change stays possible as long
as the document is changed coherently.
"""
import pathlib
import re
import unittest

ROOT = pathlib.Path(__file__).resolve().parents[1]
CONTRACT = ROOT / "docs" / "kernel-wire-contract.md"

# The vector's container id ("ct-01") and its ciphertext length, used to
# resolve the two variable-length fields of the layout table.
CONTAINER_ID_LEN = 5
CIPHERTEXT_LEN = 45


class KeyEnvelopeLayoutTests(unittest.TestCase):
    @classmethod
    def setUpClass(cls):
        cls.text = CONTRACT.read_text(encoding="utf-8")

    # -- helpers ---------------------------------------------------------

    def _section(self, start, end):
        self.assertIn(start, self.text, "contract lost section %r" % start)
        body = self.text.split(start, 1)[1]
        self.assertIn(end, body, "contract lost section %r" % end)
        return body.split(end, 1)[0]

    def _layout_fields(self):
        """Parse §3.1's field table into [(offset_expr, length, name)]."""
        section = self._section("### 3.1", "#### KeyEnvelope 固定测试向量")
        fields = []
        for line in section.splitlines():
            match = re.match(r"^(\S+)\s+(\d+|[NM])\s+(\w+)\s", line)
            if match:
                fields.append((match.group(1), match.group(2), match.group(3)))
        self.assertTrue(fields, "no field table found in §3.1")
        return fields

    def _layout_total(self, fields):
        total = 0
        for _, length, _ in fields:
            if length == "N":
                total += CONTAINER_ID_LEN
            elif length == "M":
                total += CIPHERTEXT_LEN
            else:
                total += int(length)
        return total

    def _hex_block(self, prefix):
        """Return the hex bytes of the block that starts with `prefix`.

        A block is a label line ("载荷 = ...") plus the indented lines that
        continue it. On the label line, the leading run of pure-hex
        whitespace-separated tokens is taken, so trailing prose such as
        "（帧头 8 + hello nonce 8）" is ignored; a continuation line is
        taken only when it consists of hex and whitespace alone, otherwise
        a prose line would contribute stray digits.
        """
        lines = self.text.splitlines()
        start = next((i for i, line in enumerate(lines)
                      if line.strip().startswith(prefix)), None)
        self.assertIsNotNone(start, "contract lost the %r block" % prefix)
        value = lines[start].split("=", 1)[1]
        collected = []
        for token in value.split():
            if not re.fullmatch(r"[0-9a-fA-F]+", token):
                break
            collected.append(token)
        for line in lines[start + 1:]:
            if not line.strip():
                break
            if not line[:1].isspace():
                break  # a new label line
            if re.fullmatch(r"[0-9a-fA-F\s]+", line):
                collected.append(line)
        joined = re.sub(r"\s+", "", "".join(collected))
        self.assertEqual(len(joined) % 2, 0,
                         "%r block has an odd number of hex digits" % prefix)
        return joined.lower()

    # -- the guards ------------------------------------------------------

    def test_layout_table_matches_the_aad_length(self):
        """The field table's total must equal the AAD's length field.

        The AAD is the frame header, and the frame header's length field
        counts payload bytes. If the table disagrees with the AAD, one of
        the two sides of the wire is building a different frame.
        """
        fields = self._layout_fields()
        aad = self._hex_block("AAD  =")
        self.assertEqual(len(aad), 32, "AAD must be 16 bytes")
        aad_len = int(aad[:8], 16)
        self.assertEqual(
            self._layout_total(fields), aad_len,
            "§3.1's field table totals %d bytes but the AAD's length field "
            "says %d" % (self._layout_total(fields), aad_len))

    def test_payload_listing_matches_the_aad_length(self):
        """The printed payload bytes must be as long as the AAD claims."""
        aad = self._hex_block("AAD  =")
        aad_len = int(aad[:8], 16)
        payload = self._hex_block("载荷 =")
        self.assertEqual(
            len(payload) // 2, aad_len,
            "the printed payload is %d bytes but the AAD's length field "
            "says %d" % (len(payload) // 2, aad_len))

    def test_stated_total_matches_the_aad_length(self):
        """The prose '载荷总长 = N 字节' must agree with the AAD too."""
        aad = self._hex_block("AAD  =")
        aad_len = int(aad[:8], 16)
        match = re.search(r"载荷总长\s*=\s*(\d+)\s*字节", self.text)
        self.assertIsNotNone(match, "contract lost the '载荷总长 = N 字节' line")
        self.assertEqual(
            int(match.group(1)), aad_len,
            "the contract states a payload of %s bytes while its AAD says %d"
            % (match.group(1), aad_len))

    def test_listing_contains_the_pinned_ciphertext_and_tag(self):
        """The payload listing must carry the pinned ciphertext and tag."""
        payload = self._hex_block("载荷 =")
        ciphertext = self._hex_block("密文 =")
        tag = self._hex_block("tag  =")
        self.assertIn(ciphertext, payload,
                      "the payload listing does not contain the ciphertext")
        self.assertIn(tag, payload,
                      "the payload listing does not contain the tag")

    def test_ciphertext_and_plaintext_have_the_same_length(self):
        """GCM is a stream mode: the ciphertext is as long as the plaintext.

        A one-byte difference here means a dropped hex digit in the
        document, which the fixed vectors would then pin forever.
        """
        plaintext = self._hex_block("明文 =")
        ciphertext = self._hex_block("密文 =")
        self.assertEqual(
            len(ciphertext), len(plaintext),
            "ciphertext is %d bytes but the plaintext is %d; GCM adds no "
            "padding" % (len(ciphertext) // 2, len(plaintext) // 2))

    def test_every_hex_block_has_an_even_number_of_digits(self):
        """No block may end mid-byte."""
        for prefix in ("AAD  =", "密文 =", "tag  =", "明文 =", "载荷 ="):
            with self.subTest(block=prefix):
                joined = self._hex_block(prefix)
                self.assertEqual(len(joined) % 2, 0)


if __name__ == "__main__":
    unittest.main()
