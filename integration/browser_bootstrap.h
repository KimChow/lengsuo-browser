#ifndef LENGSUO_INTEGRATION_BROWSER_BOOTSTRAP_H_
#define LENGSUO_INTEGRATION_BROWSER_BOOTSTRAP_H_
#include "base/command_line.h"
#include "base/files/file_path.h"
#include "lengsuo/integration/profile_bootstrap_state.h"
namespace blink::web_pref { struct WebPreferences; }
namespace display { struct ScreenInfos; }
namespace lengsuo {
inline constexpr char kProfileWireSwitch[] = "lengsuo-profile-wire";
bool IsManagedBrowserCommand(const base::CommandLine& command);
bool IsSafeProfilePath(const base::FilePath& path);
// Called exactly once from BrowserMainParts before BrowserProcess creation.
bool InitializeBrowserProfile(const base::CommandLine& command);
const ParsedProfile* BrowserProfile();
// Returns true only when preferences actually change.
bool ApplyTouchPreferences(const ParsedProfile& profile,
                           blink::web_pref::WebPreferences* prefs);
bool ApplyProfileTouchPreferences(blink::web_pref::WebPreferences* prefs);
// Replaces screen geometry/DPR/depth with the profile values.
bool ApplyScreenInfos(const ParsedProfile& profile,
                      display::ScreenInfos* infos);
// Profile-aware wrapper; returns false (upstream untouched) when no
// profile is loaded.
bool ApplyProfileScreenInfos(display::ScreenInfos* infos);
}  // namespace lengsuo
#endif
