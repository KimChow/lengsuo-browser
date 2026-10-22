"""Guards the machine-fact collection rules across the two repos.

Why this exists
---------------
The wire contract fixes the shape of the machine code - four fields, one
key order, one derivation - but not what each field holds. Task M0 fixes
that: the kernel and the client must collect the *same four values* on the
same machine, or the two sides derive different machine keys and the
handshake fails closed. Measured on the research machine before this card,
the two repos produced different values for the same machine (`os` was
"Ubuntu 24.04.1 LTS" on the kernel side and "linux" on the client side;
`hddid` was the device number on one side and the disk serial on the
other), so their machine codes could not have agreed.

Each side asserting its own output against its own expectation proves
nothing about the two repos agreeing. So both sides publish the same
*rule vectors* - a fixed set of machines, expressed as the four facts
plus the two digests derived from them - and this check compares them.

The tests below build a faithful pair of miniature repos and then move
one value at a time, because a check that has only ever seen the agreed
state cannot show that it would notice a disagreement.

The check recomputes the digests from the four facts with its own
implementation of the contract's derivation, so a side that published a
digest that does not follow from its own fields is caught even when the
two sides agree with each other - which is the case the per-row
comparison alone cannot see.
"""
import importlib.util
import pathlib
import tempfile
import unittest

ROOT = pathlib.Path(__file__).resolve().parents[1]


def _load_tool():
    path = ROOT / "tools" / "cross_repo_machine.py"
    spec = importlib.util.spec_from_file_location("cross_repo_machine", path)
    module = importlib.util.module_from_spec(spec)
    spec.loader.exec_module(module)
    return module


tool = _load_tool()

# The kernel's rule vectors, as the repo declares them. The digests are
# the ones the kernel's own gtest asserts, so the fixture and the shipped
# table are the same numbers.
KERNEL_VECTORS = """\
namespace lengsuo::fp {
namespace {

struct RuleVector {
  const char *label, *os, *cpuid, *hddid, *computername, *machine_code,
      *k_machine;
};

// @@begin machine-facts-vectors
inline constexpr RuleVector kRuleVectors[] = {
    {"linux-demo", "linux", "GenuineIntel/6/183/1", "DEMO-SERIAL-0001",
     "demo-host",
     "a64a48e92bef5ce24a4c3982bf33ea210a2dd5d0ad4fa5f23dc0172e7ee1409c",
     "a917d3ac761be29fcea63dc80fadd30586ee21250ea3ea56e31f5b39f47f04ec"},
    {"windows-demo", "windows", "AuthenticAMD/25/33/0", "DEMO-SERIAL-0002",
     "demo-win",
     "550635314afdee4c9b6b07370c2f6b0760b5dd6bc39cae6741645a46e8fa0891",
     "d401aa6686460d0cd36059fa0a1a05afd876414a8b4c632694e9d829e4ce5f95"},
    {"macos-demo", "macos", "Apple/0/0/0", "", "demo-mac",
     "38ac35734aedf6623c2c30c4513dbffd08f54e3442af7a51a75a5b8ad68024de",
     "cde8543cfbfde4028579b96b39ee34cdd7bc2ca612c3c3b7eb67388014ceab3f"},
    {"empty-demo", "", "", "", "",
     "c93a0514938359dddef7414908ff15348d058d8fed1cd0a238bd2e72357d065c",
     "0aea56c49b250fecbfe6c5cb4099987572519e5da82620eaf743be882815e684"},
};
// @@end machine-facts-vectors

}  // namespace
}  // namespace lengsuo::fp
"""

# The client side's rule vectors, as the client card is to declare them:
# the same rows, in Rust, with the same field names.
CLIENT_VECTORS = """\
pub(crate) struct RuleVector {
    pub(crate) label: &'static str, pub(crate) os: &'static str,
    pub(crate) cpuid: &'static str, pub(crate) hddid: &'static str,
    pub(crate) computername: &'static str, pub(crate) machine_code: &'static str,
    pub(crate) k_machine: &'static str,
}

pub(crate) const RULE_VECTORS: &[RuleVector] = &[
    RuleVector {
        label: "linux-demo", os: "linux", cpuid: "GenuineIntel/6/183/1",
        hddid: "DEMO-SERIAL-0001", computername: "demo-host",
        machine_code: "a64a48e92bef5ce24a4c3982bf33ea210a2dd5d0ad4fa5f23dc0172e7ee1409c",
        k_machine: "a917d3ac761be29fcea63dc80fadd30586ee21250ea3ea56e31f5b39f47f04ec",
    },
    RuleVector {
        label: "windows-demo", os: "windows", cpuid: "AuthenticAMD/25/33/0",
        hddid: "DEMO-SERIAL-0002", computername: "demo-win",
        machine_code: "550635314afdee4c9b6b07370c2f6b0760b5dd6bc39cae6741645a46e8fa0891",
        k_machine: "d401aa6686460d0cd36059fa0a1a05afd876414a8b4c632694e9d829e4ce5f95",
    },
    RuleVector {
        label: "macos-demo", os: "macos", cpuid: "Apple/0/0/0",
        hddid: "", computername: "demo-mac",
        machine_code: "38ac35734aedf6623c2c30c4513dbffd08f54e3442af7a51a75a5b8ad68024de",
        k_machine: "cde8543cfbfde4028579b96b39ee34cdd7bc2ca612c3c3b7eb67388014ceab3f",
    },
    RuleVector {
        label: "empty-demo", os: "", cpuid: "", hddid: "", computername: "",
        machine_code: "c93a0514938359dddef7414908ff15348d058d8fed1cd0a238bd2e72357d065c",
        k_machine: "0aea56c49b250fecbfe6c5cb4099987572519e5da82620eaf743be882815e684",
    },
];
"""


def _write(root, relative, text):
    target = root / relative
    target.parent.mkdir(parents=True, exist_ok=True)
    target.write_text(text, encoding="utf-8")
    return target


class _Pair:
    """A miniature kernel repo and client repo that do agree.

    The temporary tree is registered with the test case, so a caller
    only has to keep the pair alive.
    """

    def __init__(self, case, kernel_vectors=KERNEL_VECTORS,
                 client_vectors=CLIENT_VECTORS, client_relative=None):
        holder = tempfile.TemporaryDirectory()
        case.addCleanup(holder.cleanup)
        self.kernel = pathlib.Path(holder.name) / "kernel"
        self.client = pathlib.Path(holder.name) / "client"
        _write(self.kernel, tool.KERNEL_VECTOR_REL, kernel_vectors)
        relative = client_relative or tool.CLIENT_VECTOR_CANDIDATES[0]
        _write(self.client, relative, client_vectors)


class MachineRuleAgreementTest(unittest.TestCase):
    """The two repos' rule vectors must agree, and must notice when they do not."""

    def setUp(self):
        self.pair = _Pair(self)

    def _checks(self, pair=None):
        pair = pair or self.pair
        return tool.compare_machine(pair.kernel, pair.client)

    def _failed(self, pair=None):
        return [c for c in self._checks(pair) if not c.ok]

    def test_a_faithful_pair_agrees_on_everything(self):
        """The baseline: the two sides must agree, or nothing else means anything."""
        failed = self._failed()
        self.assertEqual(failed, [], "these checks failed: %s" %
                         [(c.label, c.values) for c in failed])

    def test_the_fixture_is_the_table_the_kernel_repo_ships(self):
        """The fixture must be the repo's own table, not a copy of it.

        The table the tool compares in anger is the one in
        `native/machine_facts_test.cc`. The fixture below is a second
        copy of it, and a copy is a drift risk: change the shipped table
        and the fixture keeps the old numbers, so every test below goes
        on checking a table nobody ships any more. This pins the copy to
        the original.
        """
        shipped = tool.kernel_rule_vectors(ROOT)
        copied = tool.kernel_rule_vectors(self.pair.kernel)
        self.assertEqual(copied, shipped,
                         "the fixture in this file and the table in %s have "
                         "drifted apart" % tool.KERNEL_VECTOR_REL)

    def test_every_row_is_checked(self):
        """Each of the four rows must produce its own check."""
        labels = [c.label for c in self._checks()]
        for row in ("linux-demo", "windows-demo", "macos-demo", "empty-demo"):
            self.assertIn("rule vector %s" % row, labels)

    # -- a moved value on either side is caught -----------------------------

    # Each of the four facts is moved in turn, on the client side and on
    # the kernel side. The kernel side matters as much as the client
    # side: without it the guard could hardcode the kernel's values and
    # still pass every other case, because the client-side tests move
    # the client's copy and a hardcoded kernel copy would follow the
    # fixture automatically.
    MOVED_FIELDS = (
        ("os", 'os: "windows",', 'os: "Windows",',
         '"linux", "GenuineIntel/6/183/1"', '"linux ", "GenuineIntel/6/183/1"'),
        ("cpuid", 'cpuid: "GenuineIntel/6/183/1"',
         'cpuid: "GenuineIntel/6/183/2"',
         '"GenuineIntel/6/183/1", "DEMO-SERIAL-0001"',
         '"GenuineIntel/6/183/2", "DEMO-SERIAL-0001"'),
        ("hddid", 'hddid: "DEMO-SERIAL-0001"', 'hddid: "DEMO-SERIAL-0009"',
         '"DEMO-SERIAL-0001"', '"DEMO-SERIAL-0009"'),
        ("computername", 'computername: "demo-host"',
         'computername: "demo-host2"', '"demo-host"', '"demo-host2"'),
    )

    def test_a_moved_fact_on_the_client_side_is_caught(self):
        """The trap: one repo changes a rule and stays green on its own tests."""
        for name, old, new, _, _ in self.MOVED_FIELDS:
            with self.subTest(field=name):
                moved = CLIENT_VECTORS.replace(old, new)
                self.assertNotEqual(moved, CLIENT_VECTORS,
                                    "the fixture must change")
                pair = _Pair(self, client_vectors=moved)
                self.assertTrue(self._failed(pair),
                                "a moved %s must be caught" % name)

    def test_a_moved_fact_on_the_kernel_side_is_caught(self):
        """The kernel's own copy must be read, not assumed."""
        for name, _, _, old, new in self.MOVED_FIELDS:
            with self.subTest(field=name):
                moved = KERNEL_VECTORS.replace(old, new)
                self.assertNotEqual(moved, KERNEL_VECTORS,
                                    "the fixture must change")
                pair = _Pair(self, kernel_vectors=moved)
                self.assertTrue(self._failed(pair),
                                "a moved kernel %s must be caught" % name)

    # -- a dropped or extra row is caught -----------------------------------

    def test_a_dropped_row_on_the_client_side_is_caught(self):
        """A side that quietly dropped a machine tests fewer machines."""
        start = CLIENT_VECTORS.index('    RuleVector {\n        label: "macos-demo"')
        end = CLIENT_VECTORS.index('    RuleVector {\n        label: "empty-demo"')
        shrunk = CLIENT_VECTORS[:start] + CLIENT_VECTORS[end:]
        self.assertNotEqual(shrunk, CLIENT_VECTORS, "the fixture must change")
        pair = _Pair(self, client_vectors=shrunk)
        failed = self._failed(pair)
        self.assertTrue(failed, "a dropped row must be caught")
        self.assertIn("rule vector set", [c.label for c in failed])

    def test_an_extra_row_on_the_client_side_is_caught(self):
        """An extra row means the two sides do not test the same machines."""
        extra = CLIENT_VECTORS.replace(
            "];\n",
            """    RuleVector {
        label: "extra-demo",
        os: "linux",
        cpuid: "GenuineIntel/6/183/1",
        hddid: "DEMO-SERIAL-0003",
        computername: "demo-extra",
        machine_code: "0000000000000000000000000000000000000000000000000000000000000000",
        k_machine: "0000000000000000000000000000000000000000000000000000000000000000",
    },
];
""", 1)
        self.assertNotEqual(extra, CLIENT_VECTORS, "the fixture must change")
        pair = _Pair(self, client_vectors=extra)
        self.assertTrue(self._failed(pair), "an extra row must be caught")

    # -- the digests must follow from the facts ----------------------------

    def test_a_hand_edited_digest_on_both_sides_is_caught(self):
        """The digests are checked against the four facts, not just each other.

        If both sides were edited the same way, the per-row comparison
        would still pass: the two sides would agree with each other and
        both would be wrong. The recomputation is what catches that.
        """
        moved = KERNEL_VECTORS.replace(
            "a64a48e92bef5ce24a4c3982bf33ea210a2dd5d0ad4fa5f23dc0172e7ee1409c",
            "b64a48e92bef5ce24a4c3982bf33ea210a2dd5d0ad4fa5f23dc0172e7ee1409c")
        self.assertNotEqual(moved, KERNEL_VECTORS, "the fixture must change")
        client = CLIENT_VECTORS.replace(
            "a64a48e92bef5ce24a4c3982bf33ea210a2dd5d0ad4fa5f23dc0172e7ee1409c",
            "b64a48e92bef5ce24a4c3982bf33ea210a2dd5d0ad4fa5f23dc0172e7ee1409c")
        self.assertNotEqual(client, CLIENT_VECTORS, "the fixture must change")
        pair = _Pair(self, kernel_vectors=moved, client_vectors=client)
        failed = self._failed(pair)
        self.assertTrue(failed, "a hand-edited digest must be caught")
        self.assertIn("kernel digests recompute", [c.label for c in failed])

    def test_a_hand_edited_k_machine_is_caught(self):
        """k_machine is derived from machine_code, so it is checked too."""
        moved = CLIENT_VECTORS.replace(
            "a917d3ac761be29fcea63dc80fadd30586ee21250ea3ea56e31f5b39f47f04ec",
            "b917d3ac761be29fcea63dc80fadd30586ee21250ea3ea56e31f5b39f47f04ec")
        self.assertNotEqual(moved, CLIENT_VECTORS, "the fixture must change")
        pair = _Pair(self, client_vectors=moved)
        failed = self._failed(pair)
        self.assertTrue(failed, "a hand-edited k_machine must be caught")
        self.assertIn("client digests recompute", [c.label for c in failed])

    # -- a side that has not published yet ----------------------------------

    def test_a_client_without_a_table_is_pending_not_ok(self):
        """The client card may not have landed. That is not a pass.

        A check that quietly reported "all fine" while one side had
        published nothing would be the same trap as an absent check
        looking like a passing one.
        """
        pair = _Pair(self, client_vectors="")
        checks = self._checks(pair)
        self.assertTrue(checks)
        for check in checks:
            if check.label.endswith("digests recompute"):
                continue
            self.assertTrue(check.pending, "%s should be pending" % check.label)
            self.assertFalse(check.ok, "%s must not read as ok" % check.label)

    def test_a_pending_check_is_not_ok(self):
        """The pending flag and the ok flag must not disagree."""
        pair = _Pair(self, client_vectors="")
        for check in self._checks(pair):
            if check.pending:
                self.assertFalse(check.ok)

    # -- the derivation is recomputed, not copied ----------------------------

    def test_the_derivation_reproduces_the_published_digests(self):
        """The tool's own derivation must agree with the shipped numbers.

        If this failed, every "digests recompute" check would be
        reporting the tool's bug rather than the table's.
        """
        facts = {
            "os": "linux",
            "cpuid": "GenuineIntel/6/183/1",
            "hddid": "DEMO-SERIAL-0001",
            "computername": "demo-host",
        }
        machine_code = tool.derive_machine_code(facts)
        self.assertEqual(
            machine_code,
            "a64a48e92bef5ce24a4c3982bf33ea210a2dd5d0ad4fa5f23dc0172e7ee1409c")
        self.assertEqual(
            tool.derive_k_machine(machine_code),
            "a917d3ac761be29fcea63dc80fadd30586ee21250ea3ea56e31f5b39f47f04ec")

    def test_the_derivation_uses_the_contract_key_order(self):
        """The JSON is the four facts in the contract's order, and no other key."""
        facts = {"os": "a", "cpuid": "b", "hddid": "c", "computername": "d"}
        self.assertEqual(
            tool.machine_json(facts),
            '{"os":"a","cpuid":"b","hddid":"c","computername":"d"}')

    # -- the client's table may live in another file ------------------------

    def test_the_client_table_is_found_wherever_the_card_put_it(self):
        """The card is allowed to split the machine module into its own file."""
        for relative in tool.CLIENT_VECTOR_CANDIDATES:
            pair = _Pair(self, client_relative=relative)
            self.assertEqual(self._failed(pair), [],
                             "the table was not found in %s" % relative)

    def test_a_kernel_table_that_cannot_be_read_is_an_error(self):
        """A kernel side that lost its table must raise, not silently pass."""
        pair = _Pair(self, kernel_vectors="// no table here\n")
        with self.assertRaises(tool.MachineVectorError):
            tool.compare_machine(pair.kernel, pair.client)


class LiveClientAgreementTest(unittest.TestCase):
    """The real client repo, when this host has it."""

    CLIENT_ROOT = pathlib.Path("/home/ubuntu/lengsuo-desktop-dev/repo")

    def test_the_two_repos_publish_the_same_rule_vectors(self):
        """The live comparison: the two repos' tables must agree.

        The client-side card is a separate lane. Until it lands, the
        check is pending, which is not a pass - so this test skips rather
        than pretending.
        """
        if not self.CLIENT_ROOT.is_dir():
            self.skipTest("the client repo is not available on this host")
        rows = tool.client_rule_vectors(self.CLIENT_ROOT)
        if rows is None:
            self.skipTest("the client side has not published its rule "
                          "vectors yet (the m0-machine-code-align card has "
                          "not landed)")
        failed = [c for c in tool.compare_machine(ROOT, self.CLIENT_ROOT)
                  if not c.ok]
        self.assertEqual(failed, [], "the two repos' rule vectors differ: %s" %
                         [(c.label, c.values) for c in failed])


if __name__ == "__main__":
    unittest.main()
