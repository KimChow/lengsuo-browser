// Unit tests for the feature-default alignment helper.
//
// The merge must be idempotent and must never discard an operator's own
// --disable-features value: this runs on every startup, and clobbering a
// debugging flag would be a silent regression in the field.
#include "lengsuo/integration/official_defaults.h"

#include <cstdio>
#include <string>

#include "base/command_line.h"

namespace {
int g_passes = 0;
int g_failures = 0;

void Check(bool ok, const char* expr, int line) {
  if (ok) {
    ++g_passes;
  } else {
    std::printf("FAIL line %d: %s\n", line, expr);
    ++g_failures;
  }
}

bool Has(std::string_view haystack, std::string_view needle) {
  return haystack.find(needle) != std::string_view::npos;
}
}  // namespace

#define CHECK(cond) Check((cond), #cond, __LINE__)

int main() {
  using lengsuo::MergeDisabledFeatures;

  // 1. Empty input still gets the required feature.
  const std::string from_empty = MergeDisabledFeatures("");
  CHECK(Has(from_empty, "ReduceAcceptLanguage"));

  // 2. An operator value is preserved, not replaced.
  const std::string merged = MergeDisabledFeatures("MyFeature,OtherThing");
  CHECK(Has(merged, "MyFeature"));
  CHECK(Has(merged, "OtherThing"));
  CHECK(Has(merged, "ReduceAcceptLanguage"));

  // 3. Idempotent: merging the result again changes nothing.
  CHECK(MergeDisabledFeatures(merged) == merged);

  // 4. No duplicates when the feature is already present.
  const std::string twice = MergeDisabledFeatures("ReduceAcceptLanguage");
  std::size_t occurrences = 0;
  for (std::size_t pos = twice.find("ReduceAcceptLanguage"); pos != std::string::npos;
       pos = twice.find("ReduceAcceptLanguage", pos + 1)) {
    ++occurrences;
  }
  CHECK(occurrences == 1);

  // 5. Whitespace and empty segments do not produce stray commas.
  const std::string messy = MergeDisabledFeatures(" A , , B ");
  CHECK(Has(messy, "A"));
  CHECK(Has(messy, "B"));
  CHECK(messy.find(",,") == std::string::npos);
  CHECK(!messy.empty() && messy.front() != ',' && messy.back() != ',');

  // 6. Applying onto a command line that has no switch sets it.
  base::CommandLine cmd(base::CommandLine::NO_PROGRAM);
  lengsuo::ApplyOfficialFeatureDefaults(&cmd);
  CHECK(cmd.HasSwitch("disable-features"));
  CHECK(Has(cmd.GetSwitchValueASCII("disable-features"),
            "ReduceAcceptLanguage"));
  // The renderer-side switch is separate and also required: the renderer never
  // rebuilds its feature list from the browser's --disable-features.
  CHECK(cmd.HasSwitch("disable-reduce-accept-language"));

  // 7. Applying twice is stable (startup may call it more than once).
  const std::string once = cmd.GetSwitchValueASCII("disable-features");
  lengsuo::ApplyOfficialFeatureDefaults(&cmd);
  CHECK(cmd.GetSwitchValueASCII("disable-features") == once);
  CHECK(cmd.HasSwitch("disable-reduce-accept-language"));

  // 8. A pre-existing switch keeps its own entries.
  base::CommandLine existing(base::CommandLine::NO_PROGRAM);
  existing.AppendSwitchASCII("disable-features", "KeepMe");
  lengsuo::ApplyOfficialFeatureDefaults(&existing);
  const std::string value = existing.GetSwitchValueASCII("disable-features");
  CHECK(Has(value, "KeepMe"));
  CHECK(Has(value, "ReduceAcceptLanguage"));

  // 9. Null-safe: callers may pass nothing in tests.
  lengsuo::ApplyOfficialFeatureDefaults(nullptr);

  std::printf("official_defaults_unittest: %d passes, %d failures\n", g_passes,
              g_failures);
  return g_failures == 0 ? 0 : 1;
}
