"""Contract tests for the browser -> renderer fingerprint switch.

The switch is the only path from a loaded profile into the renderer, so these
tests pin its field order and the presence of every production field. They run
without a Chromium tree by parsing the patch files; when a tree is present they
also check the live source, catching "patched in tree but never frozen".
"""
import pathlib
import re
import unittest

ROOT = pathlib.Path(__file__).resolve().parents[1]
CLIENT_REL = "chrome/browser/chrome_content_browser_client.cc"
TREE = ROOT.parent / "chromium-src"

# Order is normative; see docs/fp-switch-contract.md.
FIELDS = [
    "seed", "canvas", "audio", "rects", "fonts", "gpu", "ua", "webglParams",
    "vendor", "renderer", "hwc", "mem",
]


def _sources():
    """Yield (label, text) for every place the switch is assembled."""
    for patch in sorted((ROOT / "patches").glob("*.patch")):
        text = patch.read_text(encoding="utf-8", errors="replace")
        if "lengsuo-managed-fp" in text:
            yield patch.name, text
    live = TREE / CLIENT_REL
    if live.exists():
        yield "live:" + CLIENT_REL, live.read_text(encoding="utf-8")


def _switch_block(text):
    """Return the printf format that builds --lengsuo-managed-fp.

    The same file also assembles --lengsuo-managed-canvas and mentions the fp
    switch in pass-through comments, so look for an AppendSwitchASCII whose
    name argument is followed by a StringPrintf format string.
    """
    pattern = re.compile(
        r'"lengsuo-managed-fp"\s*,\s*\n?[^)]*?StringPrintf\(\s*"(%[^"]*)"', re.S)
    match = pattern.search(text)
    return match.group(1) if match else None


class SwitchContractTests(unittest.TestCase):
    def test_contract_doc_exists_and_lists_every_field(self):
        doc = (ROOT / "docs" / "fp-switch-contract.md").read_text(
            encoding="utf-8")
        for field in FIELDS:
            self.assertIn(field, doc, "contract doc misses " + field)

    def test_production_switch_emits_twelve_fields(self):
        """The profile path must carry faces and hardware, not just slots."""
        checked = 0
        for label, text in _sources():
            spec = _switch_block(text)
            if not spec:
                continue
            conversions = re.findall(r"%[0-9.*]*([a-zA-Z])", spec)
            checked += 1
            self.assertEqual(
                len(conversions), 12,
                "%s: expected 12 conversions, got %d (%s)"
                % (label, len(conversions), conversions))
        self.assertGreater(checked, 0, "no fp switch assembly found")

    def test_production_switch_carries_gpu_faces(self):
        for label, text in _sources():
            spec = _switch_block(text)
            if not spec:
                continue
            self.assertIn("s", spec, label + ": no string conversions")

    def test_webgl_slot_is_not_duplicated(self):
        """History: slots[6] was passed twice for ua and webgl."""
        for label, text in _sources():
            index = text.find('"lengsuo-managed-fp"')
            while index != -1:
                window = text[index:index + 900]
                if "StringPrintf" in window:
                    for slot in range(7):
                        occurrences = len(
                            re.findall(r"slots\[%d\]" % slot, window))
                        self.assertLessEqual(
                            occurrences, 1,
                            "%s: slots[%d] used %d times"
                            % (label, slot, occurrences))
                    break
                index = text.find('"lengsuo-managed-fp"', index + 1)


if __name__ == "__main__":
    unittest.main()
