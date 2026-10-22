// fp_target_match - the URL targeting (task T3).
//
// Two rules shape this file.
//
// The most specific target wins. Several entries of a dimension may match
// one URL; the longest url_regex is the most specific one, and a target
// with an empty pattern is the default of its dimension and loses against
// every pattern that names a URL.
//
// The matcher is ours, not the standard library's. A fingerprint product
// needs the same pattern to select the same URL on every machine, and the
// standard library does not promise that across implementations. So the
// regular expression is a small subset with pinned semantics, and the glob
// form is the one the profile layer already has - the same helper, not a
// second one.
//
// The subset is:
//
//   .        one byte
//   *  +  ?  repetition of the previous element
//   |        alternation
//   ( )      grouping
//   [ ]      a character class, with ranges and a leading ^ for negation
//   ^  $     the start and the end of the text
//   \d \D \w \W \s \S  character sets
//   \x        the byte x
//
// The whole text must match, exactly like the glob form. A pattern that
// cannot be parsed never matches; it disables its target and is never
// fatal.
#ifndef LENGSUO_NATIVE_FP_TARGET_MATCH_H_
#define LENGSUO_NATIVE_FP_TARGET_MATCH_H_

#include <string_view>

#include "fp_target.h"

namespace lengsuo::fp {

// The decision for one URL and one dimension.
struct FpTargetDecision {
  // The target that applies, or nullptr when none does.
  const FpTarget* target = nullptr;
  // True when a target applies. False means the caller keeps the existing
  // behaviour.
  bool apply = false;
  // True when the target that applies says this URL must not be perturbed
  // at all. It is only meaningful when `apply` is true.
  bool skip = false;
};

// Matches a URL against one regular expression of the subset above.
bool MatchUrlRegex(std::string_view pattern, std::string_view url);

// Picks the target that applies to `url` in `dimension`.
//
// Returns nullptr when no target applies: the caller then keeps the
// existing behaviour.
const FpTarget* SelectFpTarget(const FpTargetTable& table,
                                std::string_view url,
                                FpDimension dimension);

// The decision for one URL and one dimension.
FpTargetDecision DecideForUrl(const FpTargetTable& table,
                               std::string_view url,
                               FpDimension dimension);

}  // namespace lengsuo::fp

#endif  // LENGSUO_NATIVE_FP_TARGET_MATCH_H_
