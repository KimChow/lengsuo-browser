#ifndef LENGSUO_INTEGRATION_BROWSER_BOOTSTRAP_H_
#define LENGSUO_INTEGRATION_BROWSER_BOOTSTRAP_H_
#include <cstdint>
#include <string_view>
#include "base/command_line.h"
#include "base/files/file_path.h"
#include "lengsuo/integration/profile_bootstrap_state.h"
namespace blink::web_pref { struct WebPreferences; }
namespace display { struct ScreenInfos; }
namespace blink { struct UserAgentMetadata; struct RendererPreferences; }
namespace lengsuo {
namespace fp { struct L3Config; }
// ICU-recognized system zones only; does not override renderer timezone.
bool IsValidProfileTimezone(std::string_view zone);
inline constexpr char kProfileWireSwitch[] = "lengsuo-profile-wire";
inline constexpr char kCatalogWireSwitch[] = "lengsuo-catalog-wire";
// Managed L3 document (pins / extension whitelist / consistency rules).
inline constexpr char kL3WireSwitch[] = "lengsuo-l3-wire";
bool IsManagedBrowserCommand(const base::CommandLine& command);
bool IsSafeProfilePath(const base::FilePath& path);
// Called exactly once from BrowserMainParts before BrowserProcess creation.
bool InitializeBrowserProfile(const base::CommandLine& command);
const ParsedProfile* BrowserProfile();
const CatalogUaEntry* BrowserUa();
// GPU face for the active profile's gpu slot, or nullptr when the loaded
// catalog carries no GPU section or has no matching entry (upstream faces
// then stay untouched).
const CatalogGpuEntry* BrowserGpu();
// Loads the managed L3 document (l3_bootstrap.h) and freezes it. Returns
// false when a document was requested but could not be loaded or verified;
// the caller must abort startup rather than continue unpinned.
bool InitializeBrowserL3(const base::CommandLine& command);
// Current time in **Unix** seconds (since 1970-01-01), the unit the L3W1
// signature line uses. Exposed so the unit test can pin the epoch: reading
// base::Time::ToDeltaSinceWindowsEpoch() here would count from 1601 and
// make every document look older than the freshness window.
int64_t L3ClockUnixSeconds();
// The frozen L3 snapshot, or nullptr when this session has no L3 statement.
// Read-only and immutable after startup; the Mojo provisioning interface
// re-serializes it for each renderer that asks.
const fp::L3Config* BrowserL3Config();
bool ApplyUaMetadata(const CatalogUaEntry& entry,
                     blink::UserAgentMetadata* metadata);
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
// In-memory helper only: applies ParsedProfile::webrtc_policy to
// RendererPreferences. nullopt (schema 1) leaves everything untouched.
// Explicit 0..3 maps by checked switch onto mojom WebRtcIpHandlingPolicy,
// clears webrtc_ip_handling_urls and webrtc_local_ips_allowed_urls (the
// profile policy takes priority over per-origin overrides and mDNS local-IP
// exemptions) and retains webrtc_udp_min_port/max_port. Invalid in-memory
// values are rejected without mutation. Returns true only on change.
// Loading a schema 2 profile into the browser remains gated: startup still
// rejects v2 until runtime/network acceptance is complete.
bool ApplyWebRtcPolicy(const ParsedProfile& profile,
                       blink::RendererPreferences* prefs);
bool ApplyProfileWebRtcPolicy(blink::RendererPreferences* prefs);
}  // namespace lengsuo
#endif
