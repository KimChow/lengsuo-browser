"""Guards the cross-repo vector check against layout drift.

Why this exists
---------------
`tools/cross_repo_vectors.py` is the only executable embodiment of the
cross-repo agreement: the contract says the two repos align on "the same
set of fixed test vectors", and this script is what proves it.

It reads the kernel side's vector literal by name
(`kEnvelopePayload`). Task Q split the pipe code into five layers and
moved the shared constants into `native/kernel_pipe_test_util.h`, because
a constant defined in a .cc can only be linked into one test binary. The
tool had a single hard-coded path (`native/kernel_pipe_test.cc`) and a
single hard-coded declaration spelling (`const char`), so after the split
it failed with

    cannot read the vectors: the kernel test lost the kEnvelopePayload literal

even though the vector was present, byte-identical, and the three repos
did agree. The failure looked exactly like a cross-repo mismatch -- the
worst possible failure mode, because it points the reader at the wrong
side of the wire.

So the tool now scans candidate locations and accepts either
declaration spelling. These tests pin that behaviour: a vector in a
non-default location must still be found, and a genuinely missing vector
must still be reported (not silently skipped).

The tests also pin the thing the tool exists to protect: the payload
layout offsets it slices out of the literal.

Note on the hex below: the payload is written once, as a C++ literal, and
every test derives its bytes from that one literal through the tool's own
reader. Re-typing the bytes into a second string literal is how a
transcription slip would enter -- and a slip there looks exactly like a
protocol disagreement.
"""
import importlib.util
import pathlib
import tempfile
import unittest

ROOT = pathlib.Path(__file__).resolve().parents[1]


def _load_tool():
    path = ROOT / "tools" / "cross_repo_vectors.py"
    spec = importlib.util.spec_from_file_location("cross_repo_vectors", path)
    module = importlib.util.module_from_spec(spec)
    spec.loader.exec_module(module)
    return module


tool = _load_tool()

# The 85-byte payload, exactly as the contract's fixed vector describes it:
#   nonce_len(4) | nonce(12) | ciphertext_len(4) | ciphertext(45) |
#   tag_len(4) | tag(16)
# Declared the way task Q's shared header declares it.
PAYLOAD_LITERAL = """\
inline constexpr char kEnvelopePayload[] =
    "00 00 00 0c 00 01 02 03 "
    "04 05 06 07 08 09 0a 0b "
    "00 00 00 2d 88 3b 15 2f "
    "81 0f c5 d4 87 0a 13 99 "
    "e7 c2 6b 42 5e 51 20 47 "
    "49 37 0b 39 ee 25 cb 05 "
    "d5 f8 59 86 a4 91 70 c6 "
    "c9 b1 bb 32 72 a4 67 7e "
    "dc 00 00 00 10 c5 2e 71 "
    "8c be 62 81 f0 b8 bb b4 "
    "0c 15 37 57 ad ";
"""

# The same vector, declared the way the code declared it before the split.
PAYLOAD_LITERAL_OLD_STYLE = """\
const char kEnvelopePayload[] =
    "00 00 00 0c 00 01 02 03 "
    "04 05 06 07 08 09 0a 0b "
    "00 00 00 2d 88 3b 15 2f "
    "81 0f c5 d4 87 0a 13 99 "
    "e7 c2 6b 42 5e 51 20 47 "
    "49 37 0b 39 ee 25 cb 05 "
    "d5 f8 59 86 a4 91 70 c6 "
    "c9 b1 bb 32 72 a4 67 7e "
    "dc 00 00 00 10 c5 2e 71 "
    "8c be 62 81 f0 b8 bb b4 "
    "0c 15 37 57 ad ";
"""


class CrossRepoVectorLookupTest(unittest.TestCase):
    """The kernel-side vector must be found wherever it lives."""

    def setUp(self):
        # The temporary tree must outlive the helper call: the directory is
        # removed when its TemporaryDirectory context exits, and the lookup
        # happens after the helper returns.
        holder = tempfile.TemporaryDirectory()
        self.addCleanup(holder.cleanup)
        self.kernel = pathlib.Path(holder.name)

    def _write_vector_at(self, relative, text=PAYLOAD_LITERAL):
        target = self.kernel / relative
        target.parent.mkdir(parents=True, exist_ok=True)
        target.write_text(text, encoding="utf-8")
        return target

    def test_finds_vector_in_the_shared_header(self):
        """The layout task Q shipped: constants live in a header."""
        self._write_vector_at("native/kernel_pipe_test_util.h")
        payload = tool.find_kernel_payload(self.kernel)
        self.assertEqual(len(payload), tool.PAYLOAD_LEN * 2)

    def test_finds_vector_in_the_test_source(self):
        """The earlier layout: constants live in the test .cc."""
        self._write_vector_at("native/kernel_pipe_test.cc")
        payload = tool.find_kernel_payload(self.kernel)
        self.assertEqual(len(payload), tool.PAYLOAD_LEN * 2)

    def test_accepts_the_const_char_declaration(self):
        """Both declaration spellings must be read."""
        self._write_vector_at("native/kernel_pipe_test.cc",
                              PAYLOAD_LITERAL_OLD_STYLE)
        payload = tool.find_kernel_payload(self.kernel)
        self.assertEqual(len(payload), tool.PAYLOAD_LEN * 2)

    def test_header_wins_when_both_exist(self):
        """Both layouts present: the shared header is the more specific one."""
        self._write_vector_at("native/kernel_pipe_test_util.h")
        self._write_vector_at("native/kernel_pipe_test.cc",
                              "const char kEnvelopePayload[] = \"00\";")
        payload = tool.find_kernel_payload(self.kernel)
        self.assertEqual(len(payload), tool.PAYLOAD_LEN * 2)

    def test_missing_vector_is_reported(self):
        """A genuinely absent vector must raise, not silently pass."""
        self._write_vector_at("native/kernel_pipe_test.cc",
                              "int main() { return 0; }\n")
        with self.assertRaises(tool.VectorError):
            tool.find_kernel_payload(self.kernel)

    def test_override_path_is_honoured(self):
        """An explicit --kernel-test still wins over the candidates."""
        other = self.kernel / "elsewhere.cc"
        other.write_text(PAYLOAD_LITERAL, encoding="utf-8")
        payload = tool.find_kernel_payload(pathlib.Path("/nonexistent"),
                                          str(other))
        self.assertEqual(len(payload), tool.PAYLOAD_LEN * 2)

    def test_the_real_repo_ships_the_vector(self):
        """This repository itself must satisfy the tool's own lookup."""
        payload = tool.find_kernel_payload(ROOT)
        self.assertEqual(len(payload), tool.PAYLOAD_LEN * 2)


class CrossRepoVectorOffsetsTest(unittest.TestCase):
    """The offsets the tool slices are the contract's layout."""

    def setUp(self):
        self.payload = tool.find_kernel_payload(ROOT)

    def test_ciphertext_and_tag_offsets_locate_the_right_bytes(self):
        ciphertext = self.payload[tool.CIPHERTEXT_OFFSET * 2:
                                  (tool.CIPHERTEXT_OFFSET +
                                   tool.CIPHERTEXT_LEN) * 2]
        tag = self.payload[tool.TAG_OFFSET * 2:
                           (tool.TAG_OFFSET + tool.TAG_LEN) * 2]
        self.assertEqual(len(ciphertext), tool.CIPHERTEXT_LEN * 2)
        self.assertEqual(len(tag), tool.TAG_LEN * 2)
        # The tag is the value the contract pins, so it is the one place a
        # byte-level disagreement can be checked against a second source.
        self.assertTrue(tag.startswith("c52e718cbe6281f0"))

    def test_clear_region_forbids_the_encrypted_values(self):
        """The clear region must not repeat container id or expiry."""
        for forbidden in tool.CLEAR_REGION_FORBIDDEN:
            self.assertNotIn(forbidden, self.payload)


if __name__ == "__main__":
    unittest.main()
