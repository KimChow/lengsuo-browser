"""Diagnose where navigator.languages loses its fallback entries.

The renderer demonstrably receives `accept_languages=[en-US,en]` (proved by the
web_view_impl log), yet the page sees `["en-US"]`. This logs the value at the
point NavigatorLanguage reads it, plus whether the ReduceAcceptLanguage branch
fires - that branch truncates to the first entry by design.
"""

from pathlib import Path

SRC = Path(
    "/home/ubuntu/lengsuo-browser-dev/src/third_party/blink/renderer/core/frame/"
    "navigator_language.cc")

INCLUDE_ANCHOR = '#include "third_party/blink/renderer/platform/language.h"\n'
LOGGING = '#include "base/logging.h"\n'

OLD = """  if (languages_dirty_) {
    languages_ = ParseAndSanitize(GetAcceptLanguages());
"""
NEW = """  if (languages_dirty_) {
    const String lq_raw = GetAcceptLanguages();
    LOG(ERROR) << "LQDIAG nav.raw=[" << lq_raw.Utf8() << "]";
    languages_ = ParseAndSanitize(lq_raw);
    LOG(ERROR) << "LQDIAG nav.parsed=" << languages_.size();
    LOG(ERROR) << "LQDIAG reduce_flag="
               << base::FeatureList::IsEnabled(
                      network::features::kReduceAcceptLanguage)
               << " disable_switch="
               << base::CommandLine::ForCurrentProcess()->HasSwitch(
                      blink::switches::kDisableReduceAcceptLanguage);
"""


def main() -> int:
    text = SRC.read_text(encoding="utf-8")
    if LOGGING not in text:
        assert INCLUDE_ANCHOR in text, "include anchor missing"
        text = text.replace(INCLUDE_ANCHOR, INCLUDE_ANCHOR + LOGGING, 1)
    if "LQDIAG nav.raw" in text:
        print("already instrumented")
    else:
        assert text.count(OLD) == 1, "anchor not unique"
        text = text.replace(OLD, NEW, 1)
        print("instrumented")
    SRC.write_bytes(text.encode("utf-8"))
    return 0


if __name__ == "__main__":
    raise SystemExit(main())
