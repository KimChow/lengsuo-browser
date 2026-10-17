// Requires Chromium base/gtest build; standalone tests live separately.
#include "lengsuo/integration/browser_bootstrap.h"
#include "build/build_config.h"
#include "third_party/blink/public/common/web_preferences/web_preferences.h"
#include "testing/gtest/include/gtest/gtest.h"
#include "ui/display/screen_infos.h"
#include "ui/gfx/geometry/rect.h"
#include "third_party/blink/public/common/user_agent/user_agent_metadata.h"
namespace {
#include "lengsuo/native/golden.inc"
constexpr unsigned char kGoldenCatalog[] = {
    76,  66,  80,  67,  49,  0,   0,   0,   1,   0,   2,   112,
    49,  0,   1,   0,   2,   117, 97,  0,   0,   0,   7,   0,
    19,  83,  121, 110, 116, 104, 47,  49,  46,  48,  32,  84,
    101, 115, 116, 32,  69,  110, 118, 55,  2,   0,   5,   65,
    108, 112, 104, 97,  0,   3,   49,  46,  50,  0,   4,   66,
    101, 116, 97,  0,   3,   51,  46,  52,  2,   0,   5,   65,
    108, 112, 104, 97,  0,   7,   49,  46,  50,  46,  55,  46,
    56,  0,   4,   66,  101, 116, 97,  0,   7,   51,  46,  52,
    46,  57,  46,  48,  0,   6,   84,  101, 115, 116, 79,  83,
    0,   6,   49,  48,  46,  48,  46,  48,  0,   3,   120, 56,
    54,  0,   2,   54,  52,  0,   0,   0};
std::string CatalogWire() {
  return std::string(reinterpret_cast<const char*>(kGoldenCatalog),
                     sizeof(kGoldenCatalog));
}
}
namespace lengsuo {
TEST(BrowserBootstrap, RejectsSchemaTwoUntilPolicyInjectionIsAccepted) {
  std::string wire(reinterpret_cast<const char*>(kGolden), sizeof(kGolden));
  wire[8] = 2;
  wire.insert(65, 1, static_cast<char>(3));
  auto parsed = DecodeProfile(std::span<const std::uint8_t>(
      reinterpret_cast<const std::uint8_t*>(wire.data()), wire.size()));
  ASSERT_TRUE(parsed);
  ASSERT_EQ(2u, parsed->schema_version);
  ProfileBootstrapState state;
  EXPECT_FALSE(state.Initialize(true, true,
      [&]() -> std::optional<std::string> { return wire; }));
  EXPECT_EQ(nullptr, state.profile());
  EXPECT_FALSE(state.Initialize(true, true,
      [&]() -> std::optional<std::string> { return wire; }));
}
TEST(BrowserBootstrap, CatalogBindingFreezesAndRejectsInvalidOrMissingSlot) {
  for (int scenario : {0, 1, 2, 3, 4, 5}) {
    ProfileBootstrapState state;
    ASSERT_TRUE(state.Initialize(true, true, []() -> std::optional<std::string> {
      return std::string(reinterpret_cast<const char*>(kGolden), sizeof(kGolden));
    }));
    auto wire = CatalogWire();
    if (scenario == 1) wire[12] = '2';  // Different pool p2.
    if (scenario == 2) wire[22] = 8;    // Different UA slot.
    if (scenario == 3) wire.clear();
    if (scenario == 4) wire.resize(4097, 'x');
    if (scenario == 5) wire = "bad";
    int reads = 0;
    const bool initialized = state.InitializeCatalog(true, true,
        [&]() -> std::optional<std::string> { ++reads; return wire; });
    EXPECT_EQ(scenario == 0, initialized);
    EXPECT_EQ(1, reads);
    if (scenario == 0) {
      ASSERT_NE(nullptr, state.ua());
      EXPECT_EQ("Synth/1.0 Test Env7", state.ua()->ua_string);
    } else {
      EXPECT_EQ(nullptr, state.ua());
    }
    const auto* frozen = state.ua();
    EXPECT_FALSE(state.InitializeCatalog(true, true,
        []() -> std::optional<std::string> { return CatalogWire(); }));
    EXPECT_EQ(frozen, state.ua());
  }
}
TEST(BrowserBootstrap, UaMetadataUsesEveryCatalogFieldAndPreservesAbsentFields) {
  auto bytes = CatalogWire();
  auto catalog = DecodeCatalog(std::span<const std::uint8_t>(
      reinterpret_cast<const std::uint8_t*>(bytes.data()), bytes.size()));
  ASSERT_TRUE(catalog);
  blink::UserAgentMetadata metadata;
  metadata.full_version = "upstream";
  metadata.wow64 = true;
  metadata.form_factors = {"Desktop"};
  metadata.brand_version_list.emplace_back("old", "0");
  ASSERT_TRUE(ApplyUaMetadata(catalog->ua_entries[0], &metadata));
  ASSERT_EQ(2u, metadata.brand_version_list.size());
  EXPECT_EQ("Alpha", metadata.brand_version_list[0].brand);
  EXPECT_EQ("1.2", metadata.brand_version_list[0].version);
  ASSERT_EQ(2u, metadata.brand_full_version_list.size());
  EXPECT_EQ("Beta", metadata.brand_full_version_list[1].brand);
  EXPECT_EQ("3.4.9.0", metadata.brand_full_version_list[1].version);
  EXPECT_EQ("TestOS", metadata.platform);
  EXPECT_EQ("10.0.0", metadata.platform_version);
  EXPECT_EQ("x86", metadata.architecture);
  EXPECT_EQ("64", metadata.bitness);
  EXPECT_TRUE(metadata.model.empty());
  EXPECT_FALSE(metadata.mobile);
  EXPECT_EQ("upstream", metadata.full_version);
  EXPECT_TRUE(metadata.wow64);
  EXPECT_EQ(std::vector<std::string>({"Desktop"}), metadata.form_factors);
  auto entry = catalog->ua_entries[0];
  entry.mobile = true;
  entry.model = "Synthetic phone";
  entry.full_versions.clear();
  EXPECT_TRUE(ApplyUaMetadata(entry, &metadata));
  EXPECT_TRUE(metadata.mobile);
  EXPECT_EQ("Synthetic phone", metadata.model);
  EXPECT_TRUE(metadata.brand_full_version_list.empty());
}

TEST(BrowserBootstrap, CatalogRequiresProfileAndSafePathAndCannotBeRetried) {
  ProfileBootstrapState absent;
  int reads = 0;
  auto reader = [&]() -> std::optional<std::string> {
    ++reads;
    return "invalid";
  };
  EXPECT_FALSE(absent.InitializeCatalog(true, true, reader));
  EXPECT_EQ(0, reads);
  EXPECT_EQ(nullptr, absent.ua());
  EXPECT_FALSE(absent.InitializeCatalog(false, false, reader));
  ProfileBootstrapState state;
  ASSERT_TRUE(state.Initialize(true, true, []() -> std::optional<std::string> {
    return std::string(reinterpret_cast<const char*>(kGolden), sizeof(kGolden));
  }));
  EXPECT_FALSE(state.InitializeCatalog(true, false, reader));
  EXPECT_EQ(0, reads);
  EXPECT_FALSE(state.InitializeCatalog(true, true, reader));
  EXPECT_EQ(0, reads);
  EXPECT_EQ(nullptr, state.ua());
}
TEST(BrowserBootstrap, RefusesEmptyRelativeTraversalAndNetworkPaths) {
  EXPECT_FALSE(IsSafeProfilePath(base::FilePath()));
  EXPECT_FALSE(IsSafeProfilePath(base::FilePath(FILE_PATH_LITERAL("profile.bin"))));
  EXPECT_FALSE(IsSafeProfilePath(base::FilePath(FILE_PATH_LITERAL("//server/share/p.bin"))));
  EXPECT_FALSE(IsSafeProfilePath(base::FilePath(FILE_PATH_LITERAL("/tmp/../p.bin"))));
#if BUILDFLAG(IS_WIN)
  EXPECT_FALSE(IsSafeProfilePath(base::FilePath(FILE_PATH_LITERAL("\\\\server\\share\\p.bin"))));
  EXPECT_FALSE(IsSafeProfilePath(base::FilePath(FILE_PATH_LITERAL("\\\\?\\C:\\p.bin"))));
#endif
}
TEST(BrowserBootstrap, SwitchPresenceDoesNotImplyValidPathOrAuthorization) {
  base::CommandLine command(base::CommandLine::NO_PROGRAM);
  EXPECT_FALSE(IsManagedBrowserCommand(command));
  command.AppendSwitch(kProfileWireSwitch);
  EXPECT_TRUE(IsManagedBrowserCommand(command));
  EXPECT_FALSE(InitializeBrowserProfile(command));
  EXPECT_EQ(nullptr, BrowserProfile());
  // Failed startup cannot be replaced by a later no-profile initialization.
  base::CommandLine other(base::CommandLine::NO_PROGRAM);
  EXPECT_FALSE(InitializeBrowserProfile(other));
  EXPECT_EQ(nullptr, BrowserProfile());
}
TEST(BrowserBootstrap, TouchPreferencesAreConsistentAndIdempotent) {
  for (int points : {0, 1, 5, 10}) {
    ParsedProfile profile;
    profile.max_touch_points = points;
    blink::web_pref::WebPreferences prefs;
    ApplyTouchPreferences(profile, &prefs);
    EXPECT_EQ(points, prefs.pointer_events_max_touch_points);
    EXPECT_EQ(points != 0, prefs.touch_event_feature_detection_enabled);
    const int pointer = points ? 2 : 4;
    const int hover = points ? 1 : 2;
    EXPECT_EQ(pointer, prefs.available_pointer_types);
    EXPECT_EQ(pointer, static_cast<int>(prefs.primary_pointer_type));
    EXPECT_EQ(hover, prefs.available_hover_types);
    EXPECT_EQ(hover, static_cast<int>(prefs.primary_hover_type));
    EXPECT_FALSE(ApplyTouchPreferences(profile, &prefs));
  }
}
TEST(BrowserBootstrap, GoldenWireInitializesFrozenStateAndAppliesTouch) {
  ProfileBootstrapState state;
  int reads = 0;
  const auto reader = [&]() -> std::optional<std::string> {
    ++reads;
    return std::string(reinterpret_cast<const char*>(kGolden), sizeof(kGolden));
  };
  ASSERT_TRUE(state.Initialize(true, true, reader));
  const auto* profile = state.profile();
  ASSERT_NE(nullptr, profile);
  EXPECT_EQ(1, reads);
  EXPECT_EQ(0u, profile->max_touch_points);
  EXPECT_EQ(1920u, profile->width);
  EXPECT_EQ(1080u, profile->height);
  EXPECT_EQ(Platform::Windows, profile->platform);
  EXPECT_EQ("env-1", profile->environment_id);
  EXPECT_FALSE(state.Initialize(true, true, reader));
  EXPECT_EQ(profile, state.profile());
  EXPECT_EQ(1, reads);
  // Touch=0 maps to fine pointer and hover through the shared applicator.
  blink::web_pref::WebPreferences prefs;
  EXPECT_TRUE(ApplyTouchPreferences(*profile, &prefs));
  EXPECT_EQ(4, prefs.available_pointer_types);
  EXPECT_EQ(2, prefs.available_hover_types);
  EXPECT_EQ(0, prefs.pointer_events_max_touch_points);
  EXPECT_FALSE(ApplyTouchPreferences(*profile, &prefs));
}
TEST(BrowserBootstrap, ScreenInfosOverrideFollowsProfilePresence) {
  display::ScreenInfo default_info;
  default_info.rect = gfx::Rect(0, 0, 800, 600);
  default_info.device_scale_factor = 1.f;
  display::ScreenInfos infos(default_info);
  // No profile: upstream values stay untouched.
  EXPECT_FALSE(ApplyProfileScreenInfos(&infos));
  EXPECT_EQ(800, infos.current().rect.width());
  // With the golden profile loaded through the state reader: full override.
  ProfileBootstrapState state;
  ASSERT_TRUE(state.Initialize(true, true,
                               [&]() -> std::optional<std::string> {
                                 return std::string(
                                     reinterpret_cast<const char*>(kGolden),
                                     sizeof(kGolden));
                               }));
  ASSERT_NE(nullptr, state.profile());
  EXPECT_TRUE(ApplyScreenInfos(*state.profile(), &infos));
  EXPECT_EQ(1920, infos.current().rect.width());
  EXPECT_EQ(1080, infos.current().rect.height());
  EXPECT_EQ(0, infos.current().rect.x());
  EXPECT_EQ(0, infos.current().rect.y());
  EXPECT_EQ(1920, infos.current().available_rect.width());
  EXPECT_EQ(1080, infos.current().available_rect.height());
  EXPECT_FLOAT_EQ(1.f, infos.current().device_scale_factor);
  EXPECT_EQ(24, infos.current().depth);
}
}  // namespace lengsuo
