#include "lengsuo/integration/browser_bootstrap.h"
#include <string>
#include "base/files/file_util.h"
#include "base/no_destructor.h"
#include "third_party/blink/public/common/web_preferences/web_preferences.h"
#include "build/build_config.h"
#if BUILDFLAG(IS_WIN)
#include <windows.h>
#endif
namespace lengsuo {
namespace {
ProfileBootstrapState& State() {
  static base::NoDestructor<ProfileBootstrapState> state;
  return *state;
}
}  // namespace
bool IsManagedBrowserCommand(const base::CommandLine& command) {
  return command.HasSwitch(kProfileWireSwitch);
}
bool IsSafeProfilePath(const base::FilePath& path) {
  if (path.empty() || !path.IsAbsolute() || path.IsNetwork() ||
      path.ReferencesParent()) return false;
#if BUILDFLAG(IS_WIN)
  // Reject UNC, device namespaces and mapped network drives. Only a local
  // fixed drive is supported for this first Windows integration.
  const auto& value = path.value();
  if (value.size() < 3 || value[1] != L':' ||
      (value[2] != L'\\' && value[2] != L'/')) return false;
  std::wstring root = value.substr(0, 3);
  if (::GetDriveTypeW(root.c_str()) != DRIVE_FIXED) return false;
#endif
  return true;
}
bool InitializeBrowserProfile(const base::CommandLine& command) {
  const bool requested = IsManagedBrowserCommand(command);
  const auto path = command.GetSwitchValuePath(kProfileWireSwitch);
  return State().Initialize(requested, IsSafeProfilePath(path), [&]()
      -> std::optional<std::string> {
    // Runner owns this local file and must prevent replacement/redirection.
    // The bounded read is not a wall-clock timeout or an authorization check.
    base::File::Info info;
    if (base::IsLink(path) || !base::GetFileInfo(path, &info) || info.is_directory ||
        info.is_symbolic_link || info.size < 1 || info.size > 4096)
      return std::nullopt;
    std::string bytes;
    if (!base::ReadFileToStringWithMaxSize(path, &bytes, 4096))
      return std::nullopt;
    return bytes;
  });
}
const ParsedProfile* BrowserProfile() { return State().profile(); }
bool ApplyProfileTouchPreferences(blink::web_pref::WebPreferences* prefs) {
  const auto* profile = BrowserProfile();
  if (!profile) return false;
  return ApplyTouchPreferences(*profile, prefs);
}
bool ApplyTouchPreferences(const ParsedProfile& profile,
                           blink::web_pref::WebPreferences* prefs) {
  const bool touch = profile.max_touch_points != 0;
  using blink::mojom::PointerType;
  using blink::mojom::HoverType;
  const auto pointer = touch ? PointerType::kPointerCoarseType
                             : PointerType::kPointerFineType;
  const auto hover = touch ? HoverType::kHoverNone : HoverType::kHoverHoverType;
  const int pointers = static_cast<int>(pointer);
  const int hovers = static_cast<int>(hover);
  const bool changed =
      prefs->pointer_events_max_touch_points != profile.max_touch_points ||
      prefs->touch_event_feature_detection_enabled != touch ||
      prefs->available_pointer_types != pointers ||
      prefs->primary_pointer_type != pointer ||
      prefs->available_hover_types != hovers || prefs->primary_hover_type != hover;
  prefs->pointer_events_max_touch_points = profile.max_touch_points;
  prefs->touch_event_feature_detection_enabled = touch;
  prefs->available_pointer_types = pointers;
  prefs->primary_pointer_type = pointer;
  prefs->available_hover_types = hovers;
  prefs->primary_hover_type = hover;
  return changed;
}
}  // namespace lengsuo
