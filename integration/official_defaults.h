// official_defaults - keep feature defaults aligned with shipped Chrome.
//
// Our engine is Chromium, but the product is meant to be indistinguishable from
// a real Chrome install. Chromium's own feature defaults legitimately differ
// from Chrome's: a feature can be disabled in the published browser while the
// open-source default (or the local build configuration) leaves it on. When
// that happens the difference is invisible in the UI and shows up only in a
// fingerprint field, which is exactly the class of problem that is expensive to
// find later.
//
// `ReduceAcceptLanguage` is the worked example. Chromium truncates
// `navigator.languages` to the first entry when it is enabled, so a page sees
// ["en-US"] instead of ["en-US","en"]. Official Chrome leaves it off, so the
// two disagree on identical hardware and locale. Measured on the research host:
// our build reported ["en-US"] while Chrome 144 reported ["en-US","en"], and
// passing --disable-features=ReduceAcceptLanguage made the two identical.
//
// The browser process is the right place to fix this: the renderer inherits
// --disable-features through its own command line, so one correction here
// covers the main thread, dedicated workers and service workers at once.
#ifndef LENGSUO_INTEGRATION_OFFICIAL_DEFAULTS_H_
#define LENGSUO_INTEGRATION_OFFICIAL_DEFAULTS_H_

#include <string>
#include <string_view>

namespace base {
class CommandLine;
}

namespace lengsuo {

// Features that must be off to match shipped Chrome. Each entry is a comment
// explaining the observable difference, because a bare name here is not
// reviewable later.
inline constexpr std::string_view kFeaturesChromeDisables[] = {
    // Truncates navigator.languages to one entry.
    "ReduceAcceptLanguage",
};

// Merges the list above into the process command line's --disable-features.
// Idempotent: re-running adds nothing, and an existing user-supplied value is
// preserved rather than replaced.
void ApplyOfficialFeatureDefaults(base::CommandLine* command);

// Exposed for tests: the comma-separated value that should end up in
// --disable-features given the value already present on the command line.
std::string MergeDisabledFeatures(std::string_view existing);

}  // namespace lengsuo

#endif  // LENGSUO_INTEGRATION_OFFICIAL_DEFAULTS_H_
