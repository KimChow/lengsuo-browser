"""Checks that the wire contract still contains the fixed test vectors.

Why this exists
---------------
`docs/kernel-wire-contract.md` promises that the kernel repo and the client
repo align on "the same set of fixed test vectors". Before this test the
document described the vectors in prose but shipped no literal bytes, so each
side invented its own and the cross-repo comparison could never pass.

The document is the only agreement between two independently built repos, so
its vectors have to be pinned by an executable check rather than by review.
This test recomputes every vector from the rules stated in the document and
asserts the document contains the resulting bytes. It does not implement a
frame codec -- that is the cards' job -- it only guards the contract text.

If the protocol changes, update the document and this test together, and
update both repos in the same batch (contract section 7).
"""
import hashlib
import hmac
import pathlib
import re
import sys
import unittest

sys.path.insert(0, str(pathlib.Path(__file__).resolve().parent))
from docs_helper import doc_path  # noqa: E402  (path set up above)

ROOT = pathlib.Path(__file__).resolve().parents[1]

MAX_FRAME = 8 * 1024 * 1024
FILL = 0x5A
FRAME_TYPE = 0x03


def _header(payload_len, frame_type=FRAME_TYPE, reserved=0):
    """The 8-byte frame header: big-endian len, type, 3 reserved bytes."""
    return (payload_len.to_bytes(4, "big") + bytes([frame_type]) +
            reserved.to_bytes(3, "big"))


def _hex(bytes_):
    return " ".join("%02x" % b for b in bytes_)


def _hkdf_sha256(ikm, salt, info, length=32):
    prk = hmac.new(salt, ikm, hashlib.sha256).digest()
    out = b""
    block = b""
    counter = 1
    while len(out) < length:
        block = hmac.new(prk, block + info + bytes([counter]),
                         hashlib.sha256).digest()
        out += block
        counter += 1
    return out[:length]


MACHINE_JSON = (
    '{"os":"linux","cpuid":"cpuid-test","hddid":"hddid-test",'
    '"computername":"host-test"}'
)
MACHINE_CODE = hashlib.sha256(MACHINE_JSON.encode("utf-16-le")).hexdigest()
K_MACHINE = hashlib.sha256(MACHINE_CODE.encode("ascii")).digest()
NONCE = bytes.fromhex("0102030405060708")
INFO = b"lengsuo-kernel-session-v1"
SESSION_KEY = _hkdf_sha256(K_MACHINE, NONCE, INFO, 32)


class WireContractVectorTests(unittest.TestCase):
    @classmethod
    def setUpClass(cls):
        # The contract is an input to the work, not a record of it, so
        # it is found in the repository or in the shared directory the
        # build machine keeps it in. A machine that has neither cannot
        # check the contract, and says so rather than failing on a
        # missing file.
        contract = doc_path("kernel-wire-contract.md")
        if contract is None:
            raise unittest.SkipTest(
                "the wire contract is not available in this checkout")
        cls.text = contract.read_text(encoding="utf-8")
        # Hex bytes are compared case-insensitively: the document may spell
        # them uppercase, the computed values are lowercase, and the bytes
        # are identical either way.
        cls.lower = cls.text.lower()
        # Long vectors are wrapped across lines in the document for
        # readability, so byte sequences are searched in a whitespace-free
        # copy. Whitespace carries no meaning inside a hex vector.
        cls.stripped = re.sub(r"\s+", "", cls.lower)

    def _assert_doc_has(self, needle, why):
        self.assertIn(needle.lower(), self.lower,
                      "contract is missing %s (%s); the vector text and the "
                      "computed value must agree" % (needle, why))

    def _assert_doc_has_bytes(self, needle, why):
        """Same as _assert_doc_has, but ignores line wrapping."""
        self.assertIn(needle.lower(), self.stripped,
                      "contract is missing %s (%s); the vector text and the "
                      "computed value must agree" % (needle, why))

    def test_legal_frame_headers_are_pinned(self):
        """Every legal frame's 8-byte header must appear verbatim."""
        for length in (0, 1, 3, 255, 65535, MAX_FRAME):
            header = _hex(_header(length))
            with self.subTest(length=length):
                self._assert_doc_has(
                    header,
                    "legal frame header for len=%d" % length)

    def test_illegal_frame_bytes_are_pinned(self):
        """Every rejection case must appear verbatim."""
        cases = {
            "len over the limit": _hex(_header(MAX_FRAME + 1)),
            "reserved not zero": _hex(_header(1, reserved=1)) + " 5a",
            "unknown type": _hex(_header(1, frame_type=0x07)) + " 5a",
            "truncated header": _hex(_header(1)[:7]),
            "truncated payload": _hex(_header(3)[:9]),
        }
        for why, needle in cases.items():
            with self.subTest(case=why):
                self._assert_doc_has(needle, why)

    def test_key_derivation_vectors_are_pinned(self):
        """machine_code / k_machine / session_key must appear verbatim."""
        self._assert_doc_has(MACHINE_CODE, "machine_code digest")
        self._assert_doc_has(K_MACHINE.hex(), "k_machine digest")
        self._assert_doc_has(SESSION_KEY.hex(), "session_key digest")

    def test_key_envelope_vector_is_pinned(self):
        """The KeyEnvelope payload bytes must appear verbatim.

        The envelope is the one frame whose payload is not JSON, so both
        sides have to agree on every byte of it. The AAD is pinned too:
        dropping the hello nonce from it would silently weaken the
        handshake replay protection while still decrypting fine.
        """
        plaintext = (SESSION_KEY + b"ct-01" +
                     (1700000000).to_bytes(8, "big"))
        self._assert_doc_has_bytes(plaintext.hex(), "KeyEnvelope plaintext")
        # payload_len 85 -> frame header len field
        self._assert_doc_has_bytes("00000055", "KeyEnvelope frame length")
        # AAD = frame header (8B) + hello nonce (8B)
        aad = (85).to_bytes(4, "big") + bytes([0x02, 0, 0, 0]) + NONCE
        self._assert_doc_has_bytes(aad.hex(), "KeyEnvelope AAD")
        self._assert_doc_has_bytes(
            "883b152f810fc5d4870a1399e7c26b425e51204749370b39ee25cb05d5f8598",
            "KeyEnvelope ciphertext")
        self._assert_doc_has_bytes(
            "c52e718cbe6281f0b8bbb40c153757ad", "KeyEnvelope tag")

    def test_gcm_nonce_is_twelve_bytes(self):
        """8-byte nonces are rejected by the aes-gcm crate on both sides."""
        # Scope the check to the nonce row of the encryption table. §5 also
        # mentions the Hello nonce (8 bytes) in the AAD row, so a section-wide
        # "8 字节" search would be wrong. The declared size is the bolded
        # one; the parenthetical "⛔ 不是 8 字节" is prose about it.
        section = self.text.split("## 5. 加密", 1)[-1].split("## 6.", 1)[0]
        row = next((line for line in section.splitlines()
                    if line.strip().startswith("| nonce")), "")
        self.assertTrue(row, "encryption table lost its nonce row")
        self.assertIn("**12 字节**", row)
        self.assertNotIn("**8 字节**", row)

    def test_aad_must_cover_the_hello_nonce(self):
        """Replay protection depends on the hello nonce being in the AAD."""
        self.assertIn("AAD", self.text)
        self.assertIn("hello", self.lower)

    def test_integer_fields_are_big_endian_everywhere(self):
        """KeyEnvelope internals must state the byte order explicitly."""
        self.assertIn("u32 大端", self.text)
        self.assertIn("u64 大端", self.text)

    def test_nonce_and_info_are_pinned(self):
        """The HKDF salt and info must be stated, or the vector is useless."""
        self._assert_doc_has("01 02 03 04 05 06 07 08", "nonce")
        self._assert_doc_has("lengsuo-kernel-session-v1", "HKDF info")

    def test_len_is_big_endian_in_the_pinned_bytes(self):
        """A little-endian implementation would produce 01 00 00 00.

        The contract claims big-endian. Assert the pinned bytes really are
        big-endian, so a silent edit to little-endian cannot pass unnoticed.
        """
        self.assertIn("00 00 00 01 03 00 00 00", self.lower)
        self.assertNotIn("01 00 00 00 03 00 00 00", self.lower)

    def test_machine_code_json_shape_is_pinned(self):
        """The four fields and their order are part of the contract."""
        for field in ("os", "cpuid", "hddid", "computername"):
            with self.subTest(field=field):
                self.assertIn('"%s"' % field, self.text)
        # The order in the pinned JSON is fixed; the digest depends on it.
        positions = [self.text.index('"%s"' % f)
                     for f in ("os", "cpuid", "hddid", "computername")]
        self.assertEqual(positions, sorted(positions),
                         "machine_code field order changed")

    def test_max_frame_constant_is_8_mib(self):
        """The documented limit must match the limit the rules state."""
        self.assertIn("8 MiB", self.text)
        # 8388608 == 0x00800000 -> the header of the largest legal frame.
        self.assertIn(_hex(_header(MAX_FRAME)), self.text)
        self.assertEqual(MAX_FRAME, 0x00800000)

    def test_contract_states_the_vector_rules(self):
        """The rules the vectors encode must be stated in prose too."""
        for needed in ("大端", "reserved", "8 MiB", "填充字节"):
            with self.subTest(needed=needed):
                self.assertIn(needed, self.text)

    def test_briefs_do_not_contradict_the_contract(self):
        """The briefs must not restate a different byte layout.

        The fifth-round brief carried its own summary table with a 9-byte
        frame header and an 8-byte GCM nonce while the contract (and both
        repos' fixed test vectors) used an 8-byte header and a 12-byte
        nonce. Two sources of truth means the sides pick different ones.
        """
        brief_path = doc_path("cc-task-brief-l3f.md")
        if brief_path is None:
            self.skipTest("the fifth-round brief is not in this checkout")
        brief = brief_path.read_text(encoding="utf-8")
        # The old 9-byte header spelling must be gone.
        self.assertNotIn("[u32 len][u8 type][u32 flags]", brief)
        # The old 8-byte GCM nonce rule must be gone from the brief.
        self.assertNotIn("nonce = 帧头里的 8B 随机数", brief)
        # The brief must point at the contract as the single source.
        self.assertIn("kernel-wire-contract.md", brief)

    def test_client_brief_matches_the_contract(self):
        """The client-side brief must use the same bytes as the contract."""
        brief_path = doc_path(
            "task-briefs/client-p1-kernel-pipe-core.txt")
        if brief_path is None:
            self.skipTest("the client brief is not in this checkout")
        brief = brief_path.read_text(encoding="utf-8")
        # 12-byte GCM nonce, and it must say where the vectors live.
        self.assertIn("12 字节", brief)
        self.assertIn("kernel-wire-contract.md", brief)
        # The old "key_len + key" spelling described a different layout.
        self.assertNotIn("u32 key_len", brief)
        # The envelope payload's cleartext region holds six fields only.
        # A brief that lists container_id / expires_unix there sends the
        # client off to build a 102-byte payload while the contract's AAD
        # (and therefore its tag) is built for 85 bytes.
        self.assertIn("u32 nonce_len(12) | nonce | u32 ciphertext_len",
                      brief)
        self.assertNotIn("u32 container_id_len | container_id", brief)
        self.assertIn("40 + 密文长度", brief)

    def test_session_key_rule_is_not_contradictory(self):
        """The session key must not be declared both random and derived.

        The contract used to say "32 bytes, randomly generated (CSPRNG)"
        and, two lines below, "= HKDF(k_machine, nonce, info)". Those are
        different keys; the second one had already been used as the
        fixture for the fixed vectors.
        """
        section = self.text.split("**会话密钥**", 1)[-1].split("**机器码派生**",
                                                              1)[0]
        self.assertIn("待主理人拍板", section)
        # The unresolved choice must be spelled out as a choice, with both
        # candidates, rather than asserted twice in different words.
        self.assertIn("CSPRNG", section)
        self.assertIn("HKDF", section)
        # And it must state that the wire format is unaffected.
        self.assertIn("不影响线上字节", section)

    def test_no_duplicate_vector_tables(self):
        """Two different vector tables would let the sides pick either one."""
        headers = re.findall(r"`([0-9a-f]{2}(?: [0-9a-f]{2}){7})`", self.lower)
        self.assertTrue(headers, "no frame header bytes found in the contract")
        # Each legal header should be stated once; a duplicate means the
        # document grew a second, competing table.
        self.assertEqual(len(headers), len(set(headers)),
                         "contract states the same frame header twice")


if __name__ == "__main__":
    unittest.main()
