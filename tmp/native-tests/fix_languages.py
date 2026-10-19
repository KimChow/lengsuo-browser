"""Fix navigator.languages dropping the fallback tag.

ChromeContentBrowserClient::GetAcceptLangs() feeds
NavigatorLanguage::ParseAndSanitize(), which splits on commas. Upstream returns
the comma separated kAcceptLanguages pref (e.g. "en-US,en"). Our override
returned the bare profile tag ("en-US"), so navigator.languages collapsed to a
single entry -- a visible difference from stock Chrome that every scanner
reports.

The profile carries one primary tag, so emit that tag followed by its primary
subtag, which is what upstream produces for a single-language preference and
what stock Chrome reported in the same lab run ("en-US" -> ["en-US","en"]).
"""
from __future__ import annotations

import os
import sys

TREE = os.environ.get("CHROMIUM_SRC", "/home/ubuntu/lengsuo-browser-dev/chromium-src")
CLIENT = os.path.join(TREE, "chrome/browser/chrome_content_browser_client.cc")

OLD = """std::string ChromeContentBrowserClient::GetAcceptLangs(
    content::BrowserContext* context) {
  if (const auto* browser_profile = lengsuo::BrowserProfile())
    return browser_profile->language;"""

NEW = """std::string ChromeContentBrowserClient::GetAcceptLangs(
    content::BrowserContext* context) {
  if (const auto* browser_profile = lengsuo::BrowserProfile()) {
    // NavigatorLanguage splits this comma separated list, and
    // navigator.language is its first entry. Returning the bare tag made
    // navigator.languages a single-element array, which differs from stock
    // Chrome in every scan. Emit the tag plus its primary subtag, matching
    // what upstream produces for a single-language preference.
    const std::string& tag = browser_profile->language;
    const std::size_t dash = tag.find('-');
    if (dash != std::string::npos && dash + 1 < tag.size()) {
      return tag + "," + tag.substr(0, dash);
    }
    return tag;
  }"""


def main() -> int:
    with open(CLIENT, encoding="utf-8") as handle:
        text = handle.read()
    if "matching upstream" in text and OLD not in text:
        print("already patched")
        return 0
    if OLD not in text:
        print("anchor not found", file=sys.stderr)
        return 1
    with open(CLIENT, "w", encoding="utf-8") as handle:
        handle.write(text.replace(OLD, NEW, 1))
    print("GetAcceptLangs now reports the full preference list")
    return 0


if __name__ == "__main__":
    sys.exit(main())
