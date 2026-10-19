"""Expand the profile's language into a full accept-language list.

Chromium derives `navigator.languages` from the comma-separated
`accept_languages` renderer preference. A real install gets its value from the
`IDS_ACCEPT_LANGUAGES` locale resource, which is a full list with fallbacks
(`en-US,en`, `zh-CN,zh`, `ja`). Our profile path returned a single tag, so
`navigator.languages` collapsed to one element - on identical hardware and
locale, official Chrome reported `["en-US","en"]` and we reported `["en-US"]`.

Hardcoding a fallback tag would be wrong: it would pin every profile to two
entries and become its own constant. Chromium already ships the correct mapping
- `language::kIncognitoLanguageListMap` (87 languages, e.g.
`zh-TW -> "zh-TW,zh,en-US,en"`) - together with the fallback logic that prepends
the primary language to the global default. Reuse that instead of inventing a
table, so a profile that asks for `zh-TW` gets exactly what a real zh-TW install
reports.

The profile list may itself carry several comma-separated languages (the server
sends the full list per the reference evidence); only the first drives the
expansion, and the remainder is preserved after it.
"""

from pathlib import Path

SRC = Path(
    "/home/ubuntu/lengsuo-browser-dev/src/chrome/browser/renderer_preferences_util.cc")

INCLUDE_ANCHOR = '#include "components/language/core/browser/language_prefs.h"\n'
INCLUDE_ADD = (
    '#include "components/language/core/browser/incognito_language_list_map.h"\n')

OLD = """std::string GetLanguageListForProfile(Profile* profile,
                                      const std::string& language_list) {
  if (const auto* browser_profile = lengsuo::BrowserProfile())
    return browser_profile->language;
  return content::ReduceAcceptLanguageUtils::GetLanguagesWithMaxCount(
      profile->IsOffTheRecord()
          ? language::GetIncognitoLanguageList(language_list)
          : language_list);
}
"""

NEW = """// Expands one language tag into the full accept-language list a real install
// of that language reports, using Chromium's own table plus its fallback rule
// (prepend the primary language to the global default). Inventing a fallback
// here would make every profile report the same two-element list, which is a
// fingerprint of its own.
std::string ExpandLanguageTag(const std::string& tag) {
  for (const auto& entry : language::kIncognitoLanguageListMap) {
    if (entry.first == tag) {
      return std::string(entry.second);
    }
  }
  return base::StrCat(
      {tag, ",",
       language::kIncognitoLanguageListMap.at(
           language::kFallbackInputMethodLocale)});
}

std::string GetLanguageListForProfile(Profile* profile,
                                      const std::string& language_list) {
  if (const auto* browser_profile = lengsuo::BrowserProfile()) {
    const std::string& requested = browser_profile->language;
    if (requested.empty()) {
      return requested;
    }
    // The profile may carry a full list; expand only the primary tag and keep
    // any explicit remainder in the order the server sent it.
    const std::size_t comma = requested.find(',');
    const std::string primary = requested.substr(0, comma);
    const std::string expanded = ExpandLanguageTag(primary);
    if (comma == std::string::npos) {
      return expanded;
    }
    return base::StrCat({expanded, requested.substr(comma)});
  }
  return content::ReduceAcceptLanguageUtils::GetLanguagesWithMaxCount(
      profile->IsOffTheRecord()
          ? language::GetIncognitoLanguageList(language_list)
          : language_list);
}
"""


def main() -> int:
    text = SRC.read_text(encoding="utf-8")

    if INCLUDE_ADD in text:
        print("include already present")
    else:
        assert INCLUDE_ANCHOR in text, "include anchor missing"
        text = text.replace(INCLUDE_ANCHOR, INCLUDE_ANCHOR + INCLUDE_ADD, 1)
        print("include inserted")

    if "ExpandLanguageTag" in text:
        print("expansion already present")
    else:
        assert text.count(OLD) == 1, "anchor not unique"
        text = text.replace(OLD, NEW, 1)
        print("expansion inserted")

    SRC.write_bytes(text.encode("utf-8"))
    return 0


if __name__ == "__main__":
    raise SystemExit(main())
