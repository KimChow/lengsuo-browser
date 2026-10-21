"""Guards the two cross-repo gaps the kernel-side card Q reported.

Why this exists
---------------
The wire contract pins the *bytes on the wire* but, until 2026-10-21, it
did not pin two things the two repos must agree on before a single frame can
be exchanged:

1. how the 8-byte Hello nonce is represented inside the JSON payload;
2. which OS endpoint the ``--lengsuo-kernel-pipe=<name>`` name maps to.

Both were found by the kernel line (card Q) while it was implementing the
pipe client.  Each side would have picked a reasonable-looking encoding on
its own and the handshake would have failed at runtime with no diagnostic
pointing at the cause.

The contract is the only agreement between two independently built repos, so
these rules have to be pinned by an executable check rather than by review.
This test only guards the contract text; it does not implement a pipe client.
"""
import pathlib
import re
import unittest

ROOT = pathlib.Path(__file__).resolve().parents[1]
CONTRACT = ROOT / "docs" / "kernel-wire-contract.md"
BRIEF = ROOT / "docs" / "cc-task-brief-l3f.md"


class ContractEndpointRulesTest(unittest.TestCase):
    """The contract must state both rules unambiguously."""

    @classmethod
    def setUpClass(cls) -> None:
        cls.text = CONTRACT.read_text(encoding="utf-8")

    def test_hello_nonce_is_lowercase_hex_of_16_chars(self) -> None:
        """The 8-byte nonce must be pinned to one textual form."""
        self.assertIn("小写十六进制字符串", self.text)
        self.assertIn("16 个字符", self.text)
        # The forbidden alternatives must be called out, or a reader will
        # pick base64 and the two sides will disagree silently.
        self.assertIn("⛔ 不许用大写十六进制", self.text)
        self.assertIn("⛔ 不许带 `0x` 前缀", self.text)
        # base64 must be rejected in favour of hex, not merely mentioned.
        self.assertRegex(self.text, r"base64[^\n]*引入第二套编码")

    def test_endpoint_mapping_is_pinned_for_both_platforms(self) -> None:
        """Both the Windows and the POSIX endpoint must be stated."""
        self.assertIn(r"\\.\pipe\lengsuo-kernel-", self.text)
        self.assertIn(r"\0lengsuo-kernel-", self.text)
        # The prefix is part of the wire contract: both sides must produce
        # byte-identical endpoint names.
        self.assertIn("⛔ 前缀 `lengsuo-kernel-` 两侧必须逐字节一致", self.text)
        # A filesystem path would depend on the runtime directory and would
        # therefore not agree across the two sides.
        self.assertRegex(self.text, r"⛔[^\n]*文件系统路径")

    def test_both_rules_live_in_section_one(self) -> None:
        """The rules belong to the handshake section, not to a later one."""
        section_one = self.text.split("## 1. 启动与握手", 1)[1].split("## 2.", 1)[0]
        self.assertIn("小写十六进制字符串", section_one)
        self.assertIn(r"\0lengsuo-kernel-", section_one)

    def test_task_brief_points_at_the_contract_rules(self) -> None:
        """The brief must not restate a different encoding of its own."""
        brief = BRIEF.read_text(encoding="utf-8")
        # The brief may point at the contract ...
        self.assertIn("kernel-wire-contract.md", brief)
        # ... but must not introduce a competing spelling of the nonce.
        self.assertNotIn("nonce 编码为 base64", brief)


if __name__ == "__main__":
    unittest.main()
