// Unit tests for the feature-default alignment helper.
//
// The merge must be idempotent and must never discard an operator's own
// --disable-features value: this runs on every startup, and clobbering a
// debugging flag would be a silent regression in the field.
//
// This file used to carry its own main() and hand-rolled pass counter, which
// silently replaced the gtest runner of the bootstrap_unittests binary: every
// TEST() in the other files compiled into it never executed. It is a gtest
// TEST now, like the rest of the binary.
#include "lengsuo/integration/official_defaults.h"

#include <string>

#include "base/command_line.h"
#include "testing/gtest/include/gtest/gtest.h"

namespace {

bool Has(std::string_view haystack, std::string_view needle) {
  return haystack.find(needle) != std::string_view::npos;
}
}  // namespace

TEST(OfficialDefaults, MergeAndApply) {
  using lengsuo::MergeDisabledFeatures;

  // 1. Empty input still gets the required feature.
  const std::string from_empty = MergeDisabledFeatures("");
  EXPECT_TRUE(Has(from_empty, "ReduceAcceptLanguage"));

  // 2. An operator value is preserved, not replaced.
  const std::string merged = MergeDisabledFeatures("MyFeature,OtherThing");
  EXPECT_TRUE(Has(merged, "MyFeature"));
  EXPECT_TRUE(Has(merged, "OtherThing"));
  EXPECT_TRUE(Has(merged, "ReduceAcceptLanguage"));

  // 3. Idempotent: merging the result again changes nothing.
  EXPECT_TRUE(MergeDisabledFeatures(merged) == merged);

  // 4. No duplicates when the feature is already present.
  const std::string twice = MergeDisabledFeatures("ReduceAcceptLanguage");
  std::size_t occurrences = 0;
  for (std::size_t pos = twice.find("ReduceAcceptLanguage"); pos != std::string::npos;
       pos = twice.find("ReduceAcceptLanguage", pos + 1)) {
    ++occurrences;
  }
  EXPECT_TRUE(occurrences == 1);

  // 5. Whitespace and empty segments do not produce stray commas.
  const std::string messy = MergeDisabledFeatures(" A , , B ");
  EXPECT_TRUE(Has(messy, "A"));
  EXPECT_TRUE(Has(messy, "B"));
  EXPECT_TRUE(messy.find(",,") == std::string::npos);
  EXPECT_TRUE(!messy.empty() && messy.front() != ',' && messy.back() != ',');

  // 6. Applying onto a command line that has no switch sets it.
  base::CommandLine cmd(base::CommandLine::NO_PROGRAM);
  lengsuo::ApplyOfficialFeatureDefaults(&cmd);
  EXPECT_TRUE(cmd.HasSwitch("disable-features"));
  EXPECT_TRUE(Has(cmd.GetSwitchValueASCII("disable-features"),
                  "ReduceAcceptLanguage"));
  // The renderer-side switch is separate and also required: the renderer never
  // rebuilds its feature list from the browser's --disable-features.
  EXPECT_TRUE(cmd.HasSwitch("disable-reduce-accept-language"));

  // 7. Applying twice is stable (startup may call it more than once).
  const std::string once = cmd.GetSwitchValueASCII("disable-features");
  lengsuo::ApplyOfficialFeatureDefaults(&cmd);
  EXPECT_TRUE(cmd.GetSwitchValueASCII("disable-features") == once);
  EXPECT_TRUE(cmd.HasSwitch("disable-reduce-accept-language"));

  // 8. A pre-existing switch keeps its own entries.
  base::CommandLine existing(base::CommandLine::NO_PROGRAM);
  existing.AppendSwitchASCII("disable-features", "KeepMe");
  lengsuo::ApplyOfficialFeatureDefaults(&existing);
  const std::string value = existing.GetSwitchValueASCII("disable-features");
  EXPECT_TRUE(Has(value, "KeepMe"));
  EXPECT_TRUE(Has(value, "ReduceAcceptLanguage"));

  // 9. Null-safe: callers may pass nothing in tests.
  lengsuo::ApplyOfficialFeatureDefaults(nullptr);
}
