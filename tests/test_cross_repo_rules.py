"""Guards the profile sync *rules* layer across the two repos.

Why this exists
---------------
`docs/kernel-wire-contract.md` section 4.6 pins the collection rules
layer: the `profile_sync` section name, the 32 MiB package ceiling, the
two patterns that never leave the machine, and the glob fixed vectors
that pin the matching semantics.

The kernel implements them in `native/profile_bundle.{h,cc}` and the
client in `crates/.../kernel_pipe/profile_rules.rs`. Each side
hardcoded its own copy and each side asserted its own output against
its own copy -- the same trap task R fell into with the ConfigBundle
payload, one layer down.

This layer is the privacy floor: "Cookies and Network Cache never leave
the machine" is two separate promises if the two copies of the floor
patterns differ. The client's own docstring even says the two are
byte-identical -- but a claim in a comment is not a check.

The tests below build a faithful pair of miniature repos and then move
one value at a time, because a check that has only ever seen the agreed
state cannot show that it would notice a disagreement.

The one vector that matters most is G04 (`*History*` against
`Default/history`, no match): it is the only vector that exposes a
case-insensitive implementation. The kernel had no such vector before
this card, so a case-insensitive kernel would have kept every one of
its own tests green while computing a different manifest from the
client's.
"""
import importlib.util
import pathlib
import tempfile
import unittest

ROOT = pathlib.Path(__file__).resolve().parents[1]


def _load_tool():
    path = ROOT / "tools" / "cross_repo_rules.py"
    spec = importlib.util.spec_from_file_location("cross_repo_rules", path)
    module = importlib.util.module_from_spec(spec)
    spec.loader.exec_module(module)
    return module


tool = _load_tool()

# The contract's own text, reduced to the parts the rules layer reads.
# The values are written once here and every fixture derives from them,
# so a transcription slip would have to be made twice to go unnoticed.
CONTRACT = """\
# 线协议契约

## 4.5 profile 文件清单的形状

（本节与规则层无关，留作分节边界的夹具。）

## 4.6 profile 同步规则的固定常量与 glob 向量

### 4.6.2 固定常量（四条，两侧逐字节相同）

| 常量 | 值 | 内核侧名字 | 客户端侧名字 |
|---|---|---|---|
| `profile_sync` 段名 | `profile_sync` | `kProfileSyncConfigKey` | `PROFILE_SYNC_KEY` |
| 单包上限 | `33554432` | `kProfileBundleMaxBytes` | `MAX_PACKAGE_BYTES` |
| 地板模式 1 | `*Cookies*` | `kForbiddenCookies` | `FORBIDDEN[0]` |
| 地板模式 2 | `*Network*` | `kForbiddenNetwork` | `FORBIDDEN[1]` |

### 4.6.3 glob 固定向量（两侧必须逐条相同）

| 向量 | 模式 | 文本 | 结论 |
|---|---|---|---|
| `G01` | `*Local Storage*` | `Default/Local Storage/leveldb/0001.log` | 匹配 |
| `G02` | `*Local Storage*` | `Local Storage` | 匹配 |
| `G03` | `*History*` | `Default/History` | 匹配 |
| `G04` | `*History*` | `Default/history` | **不匹配** |
| `G05` | `*Extension State*` | `Default/Extension State/1.dat` | 匹配 |
| `G06` | `*Cache*` | `Default/Cache/data_0` | 匹配 |
| `G07` | `*Cookies*` | `Default/Cookies` | 匹配 |
| `G08` | `*Network*` | `Default/Network/Cookies` | 匹配 |
| `G09` | `History` | `History` | 匹配 |
| `G10` | `History` | `Default/History` | **不匹配** |
| `G11` | `file?.dat` | `file1.dat` | 匹配 |
| `G12` | `file?.dat` | `file12.dat` | **不匹配** |
| `G13` | `a?c` | `abc` | 匹配 |
| `G14` | `a?c` | `ac` | **不匹配** |
| `G15` | `` （空模式） | `` （空文本） | 匹配 |
| `G16` | `` （空模式） | `x` | **不匹配** |
| `G17` | `*` | `` （空文本） | 匹配 |
| `G18` | `*` | `anything` | 匹配 |

## 5. 加密
"""

# The kernel's rules layer, as the repo declares it.
KERNEL_HEADER = """\
namespace lengsuo::fp {

inline constexpr std::uint64_t kProfileBundleMaxBytes =
    32ULL * 1024ULL * 1024ULL;

inline constexpr char kProfileSyncConfigKey[] = "profile_sync";

}  // namespace lengsuo::fp
"""

KERNEL_SOURCE = """\
namespace lengsuo::fp {
namespace {

constexpr char kForbiddenCookies[] = "*Cookies*";
constexpr char kForbiddenNetwork[] = "*Network*";

}  // namespace
}  // namespace lengsuo::fp
"""

KERNEL_TEST = """\
TEST(MatchGlob, MatchesThePatternsTheListsUse) {
  EXPECT_TRUE(MatchGlob("*Local Storage*", "Default/Local Storage/leveldb/0001.log"));
  EXPECT_TRUE(MatchGlob("*Local Storage*", "Local Storage"));
  EXPECT_TRUE(MatchGlob("*History*", "Default/History"));
  EXPECT_FALSE(MatchGlob("*History*", "Default/history"));
  EXPECT_TRUE(MatchGlob("*Extension State*", "Default/Extension State/1.dat"));
  EXPECT_TRUE(MatchGlob("*Cache*", "Default/Cache/data_0"));
  EXPECT_TRUE(MatchGlob("*Cookies*", "Default/Cookies"));
  EXPECT_TRUE(MatchGlob("*Network*", "Default/Network/Cookies"));
  EXPECT_TRUE(MatchGlob("History", "History"));
  EXPECT_FALSE(MatchGlob("History", "Default/History"));
  EXPECT_TRUE(MatchGlob("file?.dat", "file1.dat"));
  EXPECT_FALSE(MatchGlob("file?.dat", "file12.dat"));
  EXPECT_TRUE(MatchGlob("a?c", "abc"));
  EXPECT_FALSE(MatchGlob("a?c", "ac"));
  EXPECT_TRUE(MatchGlob("", ""));
  EXPECT_FALSE(MatchGlob("", "x"));
  EXPECT_TRUE(MatchGlob("*", ""));
  EXPECT_TRUE(MatchGlob("*", "anything"));
}
"""

# The client's rules layer, as the repo declares it.
CLIENT_RULES = """\
pub const MAX_PACKAGE_BYTES: u64 = 32 * 1024 * 1024;

pub const PROFILE_SYNC_KEY: &str = "profile_sync";

const FORBIDDEN: [&str; 2] = ["*Cookies*", "*Network*"];

#[cfg(test)]
mod tests {
    // -- glob ---------------------------------------------------------------

    #[test]
    fn the_glob_matching_is_byte_wise_and_case_sensitive() {
        assert!(match_glob(
            "*Local Storage*",
            "Default/Local Storage/leveldb/0001.log"
        ));
        assert!(match_glob("*Local Storage*", "Local Storage"));
        assert!(match_glob("*History*", "Default/History"));
        assert!(!match_glob("*History*", "Default/history"));
        assert!(match_glob("*Extension State*", "Default/Extension State/1.dat"));
        assert!(match_glob("*Cache*", "Default/Cache/data_0"));
        assert!(match_glob("*Cookies*", "Default/Cookies"));
        assert!(match_glob("*Network*", "Default/Network/Cookies"));
        assert!(match_glob("History", "History"));
        assert!(!match_glob("History", "Default/History"));
        assert!(match_glob("file?.dat", "file1.dat"));
        assert!(!match_glob("file?.dat", "file12.dat"));
        assert!(match_glob("a?c", "abc"));
        assert!(!match_glob("a?c", "ac"));
        assert!(match_glob("", ""));
        assert!(!match_glob("", "x"));
        assert!(match_glob("*", ""));
        assert!(match_glob("*", "anything"));
    }
}
"""


def _write(root, relative, text):
    target = root / relative
    target.parent.mkdir(parents=True, exist_ok=True)
    target.write_text(text, encoding="utf-8")
    return target


class _Pair:
    """A miniature kernel repo and client repo that do agree."""

    def __init__(self, contract=CONTRACT, kernel_header=KERNEL_HEADER,
                 kernel_source=KERNEL_SOURCE, kernel_test=KERNEL_TEST,
                 client_rules=CLIENT_RULES):
        holder = tempfile.TemporaryDirectory()
        self.addCleanup = holder.cleanup
        self.kernel = pathlib.Path(holder.name) / "kernel"
        self.client = pathlib.Path(holder.name) / "client"
        _write(self.kernel, "docs/kernel-wire-contract.md", contract)
        _write(self.kernel, tool.KERNEL_HEADER_REL, kernel_header)
        _write(self.kernel, tool.KERNEL_SOURCE_REL, kernel_source)
        _write(self.kernel, tool.KERNEL_TEST_REL, kernel_test)
        _write(self.client, tool.CLIENT_RULES_REL, client_rules)


class RulesLayerAgreementTest(unittest.TestCase):
    """The two repos' rules layer must agree, and must notice when it does not."""

    def setUp(self):
        self.pair = _Pair()
        self.addCleanup(self.pair.addCleanup)

    def _failed(self):
        return [r for r in tool.compare_rules(self.pair.kernel, self.pair.client)
                if not r.ok]

    def test_a_faithful_pair_agrees_on_everything(self):
        """The baseline: the two sides must agree, or nothing else means anything."""
        failed = self._failed()
        self.assertEqual(failed, [], "these checks failed: %s" %
                         [(r.label, r.values) for r in failed])

    # -- the four constants -------------------------------------------------

    def test_a_moved_section_key_on_one_side_is_caught(self):
        """The trap: one repo renames the config section and stays green."""
        moved = CLIENT_RULES.replace(
            'pub const PROFILE_SYNC_KEY: &str = "profile_sync";',
            'pub const PROFILE_SYNC_KEY: &str = "profileSync";')
        self.assertNotEqual(moved, CLIENT_RULES, "the fixture must change")
        self.pair = _Pair(client_rules=moved)
        self.addCleanup(self.pair.addCleanup)
        self.assertTrue(self._failed(), "a moved section key must be caught")

    def test_a_moved_ceiling_on_one_side_is_caught(self):
        """The ceiling is a number, and both sides must reach the same one."""
        moved = KERNEL_HEADER.replace(
            "32ULL * 1024ULL * 1024ULL;", "64ULL * 1024ULL * 1024ULL;")
        self.assertNotEqual(moved, KERNEL_HEADER, "the fixture must change")
        self.pair = _Pair(kernel_header=moved)
        self.addCleanup(self.pair.addCleanup)
        self.assertTrue(self._failed(), "a moved ceiling must be caught")

    def test_a_different_ceiling_spelling_is_not_a_mismatch(self):
        """Both spellings mean 32 MiB, so the comparison must be by value."""
        respelled = KERNEL_HEADER.replace(
            "32ULL * 1024ULL * 1024ULL;", "33554432ULL;")
        self.assertNotEqual(respelled, KERNEL_HEADER, "the fixture must change")
        self.pair = _Pair(kernel_header=respelled)
        self.addCleanup(self.pair.addCleanup)
        self.assertEqual(self._failed(), [],
                         "the same number written differently is not drift")

    def test_a_moved_ceiling_on_the_client_side_is_caught(self):
        """The client's copy of the ceiling must be read, not assumed."""
        moved = CLIENT_RULES.replace(
            "pub const MAX_PACKAGE_BYTES: u64 = 32 * 1024 * 1024;",
            "pub const MAX_PACKAGE_BYTES: u64 = 64 * 1024 * 1024;")
        self.assertNotEqual(moved, CLIENT_RULES, "the fixture must change")
        self.pair = _Pair(client_rules=moved)
        self.addCleanup(self.pair.addCleanup)
        self.assertTrue(self._failed(),
                        "a moved client ceiling must be caught")

    def test_a_moved_floor_pattern_is_caught(self):
        """The floor is the privacy promise; both copies must say the same."""
        moved = CLIENT_RULES.replace(
            'const FORBIDDEN: [&str; 2] = ["*Cookies*", "*Network*"];',
            'const FORBIDDEN: [&str; 2] = ["*Cookie*", "*Network*"];')
        self.assertNotEqual(moved, CLIENT_RULES, "the fixture must change")
        self.pair = _Pair(client_rules=moved)
        self.addCleanup(self.pair.addCleanup)
        self.assertTrue(self._failed(), "a moved floor pattern must be caught")

    def test_a_moved_floor_pattern_on_the_kernel_side_is_caught(self):
        """The kernel's copy of the floor must be read, not assumed.

        Without this test the guard could hardcode the kernel's two
        patterns and still pass every other case: the client-side tests
        move the client's copy, and a hardcoded kernel copy would follow
        the fixture's value automatically. This is the one that keeps
        the kernel side honest.
        """
        moved = KERNEL_SOURCE.replace(
            'constexpr char kForbiddenCookies[] = "*Cookies*";',
            'constexpr char kForbiddenCookies[] = "*Cookie*";')
        self.assertNotEqual(moved, KERNEL_SOURCE, "the fixture must change")
        self.pair = _Pair(kernel_source=moved)
        self.addCleanup(self.pair.addCleanup)
        self.assertTrue(self._failed(),
                        "a moved kernel floor pattern must be caught")

    def test_a_moved_section_key_on_the_kernel_side_is_caught(self):
        """The kernel's copy of the section name must be read, too."""
        moved = KERNEL_HEADER.replace(
            'inline constexpr char kProfileSyncConfigKey[] = "profile_sync";',
            'inline constexpr char kProfileSyncConfigKey[] = "profileSync";')
        self.assertNotEqual(moved, KERNEL_HEADER, "the fixture must change")
        self.pair = _Pair(kernel_header=moved)
        self.addCleanup(self.pair.addCleanup)
        self.assertTrue(self._failed(),
                        "a moved kernel section key must be caught")

    def test_swapped_floor_patterns_are_caught(self):
        """Order is part of the constant: the client's array is positional."""
        swapped = CLIENT_RULES.replace(
            'const FORBIDDEN: [&str; 2] = ["*Cookies*", "*Network*"];',
            'const FORBIDDEN: [&str; 2] = ["*Network*", "*Cookies*"];')
        self.assertNotEqual(swapped, CLIENT_RULES, "the fixture must change")
        self.pair = _Pair(client_rules=swapped)
        self.addCleanup(self.pair.addCleanup)
        self.assertTrue(self._failed(), "swapped floor patterns must be caught")

    # -- the glob vectors ---------------------------------------------------

    def test_a_dropped_glob_vector_is_caught(self):
        """A side that stops asserting a contract vector is not verified on it."""
        shrunk = KERNEL_TEST.replace(
            '  EXPECT_FALSE(MatchGlob("*History*", "Default/history"));\n', "")
        self.assertNotEqual(shrunk, KERNEL_TEST, "the fixture must change")
        self.pair = _Pair(kernel_test=shrunk)
        self.addCleanup(self.pair.addCleanup)
        self.assertTrue(self._failed(),
                        "a dropped G04 vector must be caught")

    def test_a_flipped_glob_verdict_is_caught(self):
        """The conclusion is part of the vector, not just the inputs."""
        flipped = KERNEL_TEST.replace(
            'EXPECT_FALSE(MatchGlob("*History*", "Default/history"))',
            'EXPECT_TRUE(MatchGlob("*History*", "Default/history"))')
        self.assertNotEqual(flipped, KERNEL_TEST, "the fixture must change")
        self.pair = _Pair(kernel_test=flipped)
        self.addCleanup(self.pair.addCleanup)
        failed = self._failed()
        self.assertTrue(failed, "a flipped verdict must be caught")
        self.assertIn("glob vectors", [r.label for r in failed])

    def test_an_extra_glob_vector_on_one_side_is_caught(self):
        """An extra vector in one repo means the two sides test different sets."""
        extra = KERNEL_TEST.replace(
            '  EXPECT_TRUE(MatchGlob("*", "anything"));\n',
            '  EXPECT_TRUE(MatchGlob("*", "anything"));\n'
            '  EXPECT_TRUE(MatchGlob("*.log", "a/b.log"));\n')
        self.assertNotEqual(extra, KERNEL_TEST, "the fixture must change")
        self.pair = _Pair(kernel_test=extra)
        self.addCleanup(self.pair.addCleanup)
        self.assertTrue(self._failed(),
                        "an extra vector on one side must be caught")

    def test_the_case_sensitivity_vector_is_required_on_both_sides(self):
        """G04 is the only vector that exposes a case-insensitive matcher.

        Drop it from the contract and the disagreement it alone catches
        would go unnoticed; drop it from one side and that side is simply
        not verified on it. Both are failures, reported as such.
        """
        without = CONTRACT.replace(
            "| `G04` | `*History*` | `Default/history` | **不匹配** |\n", "")
        self.assertNotEqual(without, CONTRACT, "the fixture must change")
        self.pair = _Pair(contract=without)
        self.addCleanup(self.pair.addCleanup)
        self.assertTrue(self._failed(),
                        "dropping G04 must be caught: it is the only "
                        "case-sensitivity vector")

    # -- a side that cannot be read is an error, never a skip --------------

    def test_a_missing_client_side_is_an_error_not_a_skip(self):
        """An absent check looks exactly like a passing one."""
        (self.pair.client / tool.CLIENT_RULES_REL).unlink()
        with self.assertRaises(tool.RuleVectorError):
            tool.compare_rules(self.pair.kernel, self.pair.client)

    def test_a_contract_without_the_rules_section_is_an_error(self):
        """A contract that lost section 4.6 cannot be silently skipped."""
        without = CONTRACT.replace("### 4.6.2 固定常量", "### 4.6.2 别的")
        self.assertNotEqual(without, CONTRACT, "the fixture must change")
        self.pair = _Pair(contract=without)
        self.addCleanup(self.pair.addCleanup)
        with self.assertRaises(tool.RuleVectorError):
            tool.compare_rules(self.pair.kernel, self.pair.client)

    def test_a_client_without_its_floor_is_an_error(self):
        """No FORBIDDEN array means no floor, which is not a passing state."""
        without = CLIENT_RULES.replace(
            'const FORBIDDEN: [&str; 2] = ["*Cookies*", "*Network*"];', "")
        self.assertNotEqual(without, CLIENT_RULES, "the fixture must change")
        self.pair = _Pair(client_rules=without)
        self.addCleanup(self.pair.addCleanup)
        with self.assertRaises(tool.RuleVectorError):
            tool.compare_rules(self.pair.kernel, self.pair.client)


if __name__ == "__main__":
    unittest.main()
