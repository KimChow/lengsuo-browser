"""Patch the UA-CH brand list so an unbranded build still advertises a product.

Chromium guards the product brand behind `#if !BUILDFLAG(CHROMIUM_BRANDING)`, so
a build from the public tree lists only `Chromium` + the GREASE brand. Real
Chrome always lists `Google Chrome` as well, which makes the absence a
one-field giveaway. Route the unbranded branch through our own constant so the
label lives in one place and versions keep coming from chrome/VERSION.
"""

from pathlib import Path

SRC = Path("/home/ubuntu/lengsuo-browser-dev/src/components/embedder_support/user_agent_utils.cc")

INCLUDE_ANCHOR = '#include "components/version_info/version_info.h"\n'
INCLUDE_LINE = '#include "lengsuo/native/product_brand.h"\n'

OLD = """  std::optional<std::string> brand;
#if !BUILDFLAG(CHROMIUM_BRANDING)
  brand = version_info::GetProductName();
#endif
"""

NEW = """  std::optional<std::string> brand;
#if !BUILDFLAG(CHROMIUM_BRANDING)
  brand = version_info::GetProductName();
#else
  // Unbranded builds would otherwise advertise only `Chromium`, which no real
  // Chrome install does. The label lives in product_brand.h; the version still
  // comes from chrome/VERSION so brand and version cannot drift apart.
  brand = std::string(lengsuo::native::kProductBrand);
#endif
"""


def main() -> int:
    text = SRC.read_text(encoding="utf-8")

    if INCLUDE_LINE in text:
        print("include already present")
    else:
        assert INCLUDE_ANCHOR in text, "include anchor missing"
        text = text.replace(INCLUDE_ANCHOR, INCLUDE_ANCHOR + INCLUDE_LINE, 1)
        print("include inserted")

    if "lengsuo::native::kProductBrand" in text:
        print("brand branch already present")
    else:
        assert text.count(OLD) == 1, "brand branch anchor not unique"
        text = text.replace(OLD, NEW, 1)
        print("brand branch inserted")

    SRC.write_bytes(text.encode("utf-8"))
    return 0


if __name__ == "__main__":
    raise SystemExit(main())
