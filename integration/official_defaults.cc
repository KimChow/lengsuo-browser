#include "lengsuo/integration/official_defaults.h"

#include <array>
#include <vector>

#include "base/command_line.h"
#include "base/strings/string_split.h"
#include "base/strings/string_util.h"

namespace lengsuo {
namespace {

constexpr char kDisableFeaturesSwitch[] = "disable-features";

// Blink reads this from the renderer's own command line to decide whether to
// keep navigator.languages at full length. It has to be appended here *and*
// allow-listed in RenderProcessHostImpl's kSwitchNames; the renderer does not
// rebuild its feature list from the browser's --disable-features.
constexpr char kDisableReduceAcceptLanguageSwitch[] =
    "disable-reduce-accept-language";

// Keeps the merge order deterministic and preserves the user's own ordering,
// so an operator-supplied flag list still reads the way they wrote it.
std::string JoinUnique(const std::vector<std::string>& ordered) {
  std::vector<std::string> unique;
  for (const std::string& item : ordered) {
    if (item.empty()) {
      continue;
    }
    bool seen = false;
    for (const std::string& kept : unique) {
      if (kept == item) {
        seen = true;
        break;
      }
    }
    if (!seen) {
      unique.push_back(item);
    }
  }
  return base::JoinString(unique, ",");
}

}  // namespace

std::string MergeDisabledFeatures(std::string_view existing) {
  const std::string current(existing);
  std::vector<std::string> parts = base::SplitString(
      current, ",", base::TRIM_WHITESPACE, base::SPLIT_WANT_NONEMPTY);
  for (const std::string_view feature : kFeaturesChromeDisables) {
    parts.emplace_back(feature);
  }
  return JoinUnique(parts);
}

void ApplyOfficialFeatureDefaults(base::CommandLine* command) {
  if (command == nullptr) {
    return;
  }
  const std::string current = command->GetSwitchValueASCII(kDisableFeaturesSwitch);
  const std::string merged = MergeDisabledFeatures(current);
  if (merged != current) {
    command->AppendSwitchASCII(kDisableFeaturesSwitch, merged);
  }
  // The renderer ignores the browser's feature list; Blink exposes a dedicated
  // switch for the one behaviour we need to match. Appending it is idempotent.
  if (!command->HasSwitch(kDisableReduceAcceptLanguageSwitch)) {
    command->AppendSwitch(kDisableReduceAcceptLanguageSwitch);
  }
}

}  // namespace lengsuo
