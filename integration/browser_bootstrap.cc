#include "lengsuo/integration/browser_bootstrap.h"
#include <string>
#include "third_party/icu/source/i18n/unicode/timezone.h"
#include "base/files/file_util.h"
#include "base/no_destructor.h"
// renderer_preferences.h pulls in the mojom WebRtcIpHandlingPolicy enum.
#include "third_party/blink/public/common/renderer_preferences/renderer_preferences.h"
#include "third_party/blink/public/common/web_preferences/web_preferences.h"
#include "third_party/blink/public/common/user_agent/user_agent_metadata.h"
#include "ui/display/screen_infos.h"
#include "build/build_config.h"
#if BUILDFLAG(IS_WIN)
#include <windows.h>
#endif
namespace lengsuo {
bool IsValidProfileTimezone(std::string_view zone) {
  // Bounded ASCII-only token, then a real system zone through ICU canonical
  // lookup. Unknown, non-system (e.g. format-only) and malformed IDs fail.
  if (zone.empty() || zone.size() > 64) return false;
  for (const char byte : zone) {
    if (static_cast<unsigned char>(byte) < 33 ||
        static_cast<unsigned char>(byte) > 126)
      return false;
  }
  UErrorCode status = U_ZERO_ERROR;
  UBool system = false;
  icu::UnicodeString canonical;
  icu::TimeZone::getCanonicalID(
      icu::UnicodeString::fromUTF8(
          icu::StringPiece(zone.data(), static_cast<int32_t>(zone.size()))),
      canonical, system, status);
  return U_SUCCESS(status) && system;
}

namespace {
ProfileBootstrapState& State() {
  static base::NoDestructor<ProfileBootstrapState> state;
  return *state;
}
std::optional<std::string> ReadWire(const base::FilePath& path) {
  // Runner owns this local file and must prevent replacement/redirection.
  // Bounded reads are not wall-clock timeouts or authorization checks.
  base::File::Info info;
  if (base::IsLink(path) || !base::GetFileInfo(path, &info) || info.is_directory ||
      info.is_symbolic_link || info.size < 1 || info.size > 4096)
    return std::nullopt;
  std::string bytes;
  if (!base::ReadFileToStringWithMaxSize(path, &bytes, 4096))
    return std::nullopt;
  return bytes;
}
}  // namespace
bool IsManagedBrowserCommand(const base::CommandLine& command) {
  return command.HasSwitch(kProfileWireSwitch) ||
         command.HasSwitch(kCatalogWireSwitch);
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
  const bool requested = command.HasSwitch(kProfileWireSwitch);
  const auto path = command.GetSwitchValuePath(kProfileWireSwitch);
  if (!State().Initialize(requested, IsSafeProfilePath(path),
                          [&]() { return ReadWire(path); })) return false;
  if (const auto* profile = BrowserProfile();
      profile && !IsValidProfileTimezone(profile->timezone)) return false;
  const auto catalog_path = command.GetSwitchValuePath(kCatalogWireSwitch);
  return State().InitializeCatalog(command.HasSwitch(kCatalogWireSwitch),
      IsSafeProfilePath(catalog_path), [&]() { return ReadWire(catalog_path); });
}
const ParsedProfile* BrowserProfile() { return State().profile(); }
const CatalogUaEntry* BrowserUa() { return State().ua(); }
bool ApplyUaMetadata(const CatalogUaEntry& entry,
                     blink::UserAgentMetadata* metadata) {
  metadata->brand_version_list.clear();
  for (const auto& brand : entry.brands)
    metadata->brand_version_list.emplace_back(brand.name, brand.version);
  metadata->brand_full_version_list.clear();
  for (const auto& brand : entry.full_versions)
    metadata->brand_full_version_list.emplace_back(brand.name, brand.version);
  metadata->platform = entry.platform;
  metadata->platform_version = entry.platform_version;
  metadata->architecture = entry.architecture;
  metadata->bitness = entry.bitness;
  metadata->model = entry.model;
  metadata->mobile = entry.mobile;
  return true;
}
bool ApplyProfileTouchPreferences(blink::web_pref::WebPreferences* prefs) {
  const auto* profile = BrowserProfile();
  if (!profile) return false;
  return ApplyTouchPreferences(*profile, prefs);
}
bool ApplyScreenInfos(const ParsedProfile& profile,
                      display::ScreenInfos* infos) {
  display::ScreenInfo info = infos->current();
  // The wire stores DPR in milli-units; restore with exact integer math.
  info.device_scale_factor =
      static_cast<float>(profile.dpr_milli) / 1000.f;
  // Single-device model: one screen sized exactly like the profile. The
  // profile carries no work-area semantics, so available == full rect.
  const gfx::Rect rect(0, 0, static_cast<int>(profile.width),
                       static_cast<int>(profile.height));
  info.rect = rect;
  info.available_rect = rect;
  info.depth = profile.color_depth;
  *infos = display::ScreenInfos(info);
  return true;
}
bool ApplyProfileScreenInfos(display::ScreenInfos* infos) {
  const auto* profile = BrowserProfile();
  if (!profile) return false;
  return ApplyScreenInfos(*profile, infos);
}
bool ApplyWebRtcPolicy(const ParsedProfile& profile,
                       blink::RendererPreferences* prefs) {
  // Schema 1 statement (nullopt): upstream behavior stays untouched.
  if (!profile.webrtc_policy) return false;
  using blink::mojom::WebRtcIpHandlingPolicy;
  WebRtcIpHandlingPolicy policy;
  // Checked switch over the wire range 0..3; anything else is rejected
  // here without mutating prefs (the wire decoder already gates this).
  switch (*profile.webrtc_policy) {
    case 0: policy = WebRtcIpHandlingPolicy::kDefault; break;
    case 1: policy = WebRtcIpHandlingPolicy::kDefaultPublicAndPrivateInterfaces; break;
    case 2: policy = WebRtcIpHandlingPolicy::kDefaultPublicInterfaceOnly; break;
    case 3: policy = WebRtcIpHandlingPolicy::kDisableNonProxiedUdp; break;
    default: return false;
  }
  const bool changed = prefs->webrtc_ip_handling_policy != policy ||
                       !prefs->webrtc_ip_handling_urls.empty() ||
                       !prefs->webrtc_local_ips_allowed_urls.empty();
  // Explicit profile policy takes priority: per-origin overrides and the
  // mDNS local-IP exemption list are cleared. UDP port range is retained.
  prefs->webrtc_ip_handling_policy = policy;
  prefs->webrtc_ip_handling_urls.clear();
  prefs->webrtc_local_ips_allowed_urls.clear();
  return changed;
}
bool ApplyProfileWebRtcPolicy(blink::RendererPreferences* prefs) {
  const auto* profile = BrowserProfile();
  if (!profile) return false;
  return ApplyWebRtcPolicy(*profile, prefs);
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
