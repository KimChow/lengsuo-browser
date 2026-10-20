"""Keep navigator.languages aligned with shipped Chrome.

Chromium truncates `navigator.languages` to a single entry when
`kReduceAcceptLanguage` is enabled. On this build the feature resolves to
enabled while Chrome has it off, so identical hardware and locale produced
`["en-US"]` for us and `["en-US","en"]` for Chrome.

Two independent pieces are required to fix it, and either one alone is inert:

  * the switch must be appended at browser startup, and
  * `blink::switches::kDisableReduceAcceptLanguage` must be allow-listed for the
    renderer, because the renderer does not rebuild its feature list from the
    browser's `--disable-features`.

The second point cost real time to find, so it is pinned here: adding the
startup call without the allow-list entry looks correct and changes nothing.

Where the two pieces live
-------------------------
They do **not** live in the same patch any more. The patch series gives
`chrome_browser_main.cc` a single owner (0016), so the startup call is in
0016 while the renderer allow-list stays in 0013. The requirement that
matters is that both land in the replayed tree, not that they share a file.
"""

import pathlib
import unittest

ROOT = pathlib.Path(__file__).resolve().parents[1]
PATCH = ROOT / "patches" / "0013-official-feature-defaults.patch"
WIRING_PATCH = ROOT / "patches" / "0016-browser-wiring.patch"
HEADER = ROOT / "integration" / "official_defaults.h"
IMPL = ROOT / "integration" / "official_defaults.cc"
TEST = ROOT / "integration" / "official_defaults_unittest.cc"
BUILD_GN = ROOT / "integration" / "BUILD.gn"


class OfficialDefaultsTests(unittest.TestCase):
    def test_startup_switch_is_appended(self):
        # chrome_browser_main.cc has a single owner (0016), so the startup
        # call is asserted there.
        text = WIRING_PATCH.read_text(encoding="utf-8")
        self.assertIn("ApplyOfficialFeatureDefaults", text)
        self.assertIn("chrome_browser_main.cc", text)

    def test_renderer_switch_is_allow_listed(self):
        """Without this the startup append is silently discarded."""
        text = PATCH.read_text(encoding="utf-8")
        self.assertIn("render_process_host_impl.cc", text)
        self.assertIn("kDisableReduceAcceptLanguage", text)

    def test_both_pieces_reach_the_replayed_tree(self):
        """They only work together; splitting them invites a half-fix."""
        wiring = WIRING_PATCH.read_text(encoding="utf-8")
        allow_list = PATCH.read_text(encoding="utf-8")
        self.assertIn("chrome_browser_main.cc", wiring)
        self.assertIn("ApplyOfficialFeatureDefaults", wiring)
        self.assertIn("render_process_host_impl.cc", allow_list)
        self.assertIn("kDisableReduceAcceptLanguage", allow_list)

    def test_declares_the_required_feature(self):
        text = HEADER.read_text(encoding="utf-8")
        self.assertIn("ReduceAcceptLanguage", text)
        self.assertIn("MergeDisabledFeatures", text)

    def test_merge_preserves_existing_values(self):
        """An operator's own --disable-features must survive."""
        text = IMPL.read_text(encoding="utf-8")
        self.assertIn("SPLIT_WANT_NONEMPTY", text)
        self.assertIn("JoinUnique", text)

    def test_implementation_sets_the_renderer_switch(self):
        text = IMPL.read_text(encoding="utf-8")
        self.assertIn("disable-reduce-accept-language", text)

    def test_unit_test_covers_idempotence(self):
        text = TEST.read_text(encoding="utf-8")
        self.assertIn("MergeDisabledFeatures(merged) == merged", text)
        self.assertIn("KeepMe", text)

    def test_wired_into_build(self):
        text = BUILD_GN.read_text(encoding="utf-8")
        self.assertIn("official_defaults.cc", text)
        self.assertIn("official_defaults_unittest.cc", text)


if __name__ == "__main__":
    unittest.main()
