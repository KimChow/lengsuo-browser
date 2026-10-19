"""An unbranded build must still advertise a product brand.

Chromium hides the product brand behind ``#if !BUILDFLAG(CHROMIUM_BRANDING)``, so
a build from the public tree lists only ``Chromium`` plus the GREASE brand. Real
Chrome always lists three entries including ``Google Chrome``, which makes the
missing entry the single clearest "this is not Chrome" signal in any
client-hint check.

The fix is a one-line route through our own constant; these tests keep it from
silently disappearing, and keep the version source honest (versions must come
from chrome/VERSION, never a literal, or brand and version drift apart).
"""

import pathlib
import re
import unittest

ROOT = pathlib.Path(__file__).resolve().parents[1]
SOURCE_PATCH = ROOT / "patches" / "0011-lengsuo-sources.patch"
BRAND_PATCH = ROOT / "patches" / "0012-ua-ch-brand.patch"
BRAND_HEADER = ROOT / "native" / "product_brand.h"
OVERLAY = ROOT / "tools" / "overlay.py"
BUILD_GN = ROOT / "native" / "BUILD.gn"


class ProductBrandTests(unittest.TestCase):
    def test_header_declares_the_brand(self):
        text = BRAND_HEADER.read_text(encoding="utf-8")
        self.assertIn("kProductBrand", text)
        self.assertRegex(text, r'kProductBrand\s*=\s*"Google Chrome"')

    def test_header_does_not_hardcode_a_version(self):
        """A literal version would drift from chrome/VERSION on every bump."""
        text = BRAND_HEADER.read_text(encoding="utf-8")
        self.assertNotRegex(
            text, r'kProductBrand\s*=\s*"[^"]*\d+\.\d+',
            "brand constant must not carry a version number")

    def test_patch_routes_the_unbranded_branch(self):
        text = BRAND_PATCH.read_text(encoding="utf-8")
        self.assertIn("lengsuo/native/product_brand.h", text)
        self.assertIn("kProductBrand", text)
        # The guard must still branch on the branding flag, not replace it.
        self.assertIn("!BUILDFLAG(CHROMIUM_BRANDING)", text)

    def test_patch_keeps_branded_builds_untouched(self):
        """Branded builds already get a product name; do not double-append."""
        text = BRAND_PATCH.read_text(encoding="utf-8")
        self.assertIn("version_info::GetProductName()", text)

    def test_header_is_delivered_by_the_sources_patch(self):
        """The overlay copies files, but a clean rebuild replays the patch."""
        self.assertIn("lengsuo/native/product_brand.h",
                      SOURCE_PATCH.read_text(encoding="utf-8"))

    def test_header_is_in_the_overlay_mapping(self):
        self.assertIn("native/product_brand.h",
                      OVERLAY.read_text(encoding="utf-8"))

    def test_header_is_in_the_gn_source_set(self):
        self.assertIn("product_brand.h", BUILD_GN.read_text(encoding="utf-8"))


if __name__ == "__main__":
    unittest.main()
