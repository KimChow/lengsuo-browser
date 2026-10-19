"""Remove the temporary accept_languages diagnostics.

Two probes were added while chasing this: one in web_view_impl.cc (proving the
renderer receives `en-US,en`) and one in navigator_language.cc (proving the
parse yields two entries before a feature flag truncates it). Both are removed
here; the real fix lands in the feature default instead.
"""

from pathlib import Path

BLINK = Path("/home/ubuntu/lengsuo-browser-dev/src/third_party/blink/renderer")

WEB_VIEW = BLINK / "core/exported/web_view_impl.cc"
NAV_LANG = BLINK / "core/frame/navigator_language.cc"

WEB_VIEW_LOG = """  LOG(ERROR) << "LQDIAG accept_languages=[" << renderer_preferences_.accept_languages
             << "] old=[" << old_accept_languages << "]";
"""

NAV_LOG_OLD = """    const String lq_raw = GetAcceptLanguages();
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

NAV_LOG_NEW = """    languages_ = ParseAndSanitize(GetAcceptLanguages());
"""


def strip(path: Path, old: str, new: str, label: str) -> None:
    text = path.read_text(encoding="utf-8")
    if old not in text:
        print("%s: already clean" % label)
        return
    path.write_bytes(text.replace(old, new, 1).encode("utf-8"))
    print("%s: diagnostic removed" % label)


def main() -> int:
    strip(WEB_VIEW, WEB_VIEW_LOG, "", "web_view_impl")
    strip(NAV_LANG, NAV_LOG_OLD, NAV_LOG_NEW, "navigator_language")
    return 0


if __name__ == "__main__":
    raise SystemExit(main())
