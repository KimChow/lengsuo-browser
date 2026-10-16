// Requires Chromium base/gtest build; standalone tests live separately.
#include "lengsuo/integration/browser_bootstrap.h"
#include "build/build_config.h"
#include "third_party/blink/public/common/web_preferences/web_preferences.h"
#include "testing/gtest/include/gtest/gtest.h"
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
}  // namespace lengsuo
