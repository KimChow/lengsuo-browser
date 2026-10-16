// Requires Chromium base/gtest build; standalone tests live separately.
#include "lengsuo/integration/browser_bootstrap.h"
#include "build/build_config.h"
#include "third_party/blink/public/common/web_preferences/web_preferences.h"
#include "testing/gtest/include/gtest/gtest.h"
#include "ui/display/screen_infos.h"
#include "ui/gfx/geometry/rect.h"
namespace {
#include "lengsuo/native/golden.inc"
}
namespace lengsuo {
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
  display::ScreenInfos infos(display::ScreenInfo());
  infos.current().rect = gfx::Rect(0, 0, 800, 600);
  infos.current().device_scale_factor = 1.f;
  // No profile: upstream values stay untouched.
  EXPECT_FALSE(ApplyProfileScreenInfos(&infos));
  EXPECT_EQ(800, infos.current().rect.width());
  // With the golden profile loaded through the state reader: full override.
  namespace {
#include "lengsuo/native/golden.inc"
  }
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
  EXPECT_EQ(infos.current().rect, infos.current().available_rect);
  EXPECT_FLOAT_EQ(1.f, infos.current().device_scale_factor);
  EXPECT_EQ(24, infos.current().depth);
}
}  // namespace lengsuo
