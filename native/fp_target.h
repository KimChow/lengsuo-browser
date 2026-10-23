// fp_target - the table of fingerprint target values (task T3).
//
// The platform decides what a canvas or a webgl sample of a given URL
// should look like. The kernel receives those decisions as configuration
// and applies them; it never decides a target value itself.
//
// The table is configuration, not code. Everything in it comes from the
// ConfigBundle, so the same bundle gives the same behaviour on every
// machine, and a bundle without targets gives exactly the behaviour the
// kernel had before this task.
//
// The shape of one entry is:
//
//   dimension    "canvas" or "webgl"
//   url_regex    a pattern that selects the URLs this entry speaks for;
//                an empty pattern is the default of the dimension
//   url_glob     the same thing in the glob form; url_regex wins when
//                both are given
//   target_hash  the fingerprint the sample must be brought to
//   width        the width of the sample in pixels
//   length       the height of the sample in pixels
//   drew_text    whether the page drew text; optional, and when it is
//                absent the entry does not constrain the question
//   skip         optional; true says this URL must not be perturbed at
//                all
//
// A malformed entry is dropped and counted. It never poisons the entries
// around it and it never fails the whole table: one bad entry from the
// platform must not take the browser's fingerprints down with it.
//
// The threshold that decides whether a sample is close enough to its
// target is `similarity`, a value of the same section. It is optional, and
// without it there is no verdict at all - see fp_converge.h.
#ifndef LENGSUO_NATIVE_FP_TARGET_H_
#define LENGSUO_NATIVE_FP_TARGET_H_

#include <cstdint>
#include <optional>
#include <string>
#include <string_view>
#include <vector>

#include "fp_sampling.h"

namespace lengsuo::fp {

// The name of the configuration section that carries the table.
inline constexpr char kFpTargetConfigKey[] = "fp_target";

// One target value.
struct FpTarget {
  // The dimension this entry speaks for.
  FpDimension dimension = FpDimension::kCanvas;
  // The pattern that selects the URLs. An empty pattern is the default of
  // the dimension: it applies to every URL of that dimension that no
  // other entry claims.
  std::string url_regex;
  // The same selection in the glob form. When url_regex is not empty this
  // field is ignored.
  std::string url_glob;
  // The fingerprint the sample must be brought to.
  std::string target_hash;
  // The width of the sample in pixels.
  std::uint32_t width = 0;
  // The height of the sample in pixels.
  std::uint32_t length = 0;
  // True when the entry says whether the page drew text into the sample.
  // False when the entry does not constrain the question at all.
  bool has_drew_text = false;
  // Whether the page drew text. Only meaningful when has_drew_text.
  bool drew_text = false;
  // True when this URL must not be perturbed at all.
  bool skip = false;
};

// The table of target values.
struct FpTargetTable {
  // The entries that were read and are usable.
  std::vector<FpTarget> targets;
  // The threshold of the comparison. Absent when the platform did not
  // configure one, and then no sample can be judged.
  std::optional<std::uint32_t> similarity;
  // How many entries were dropped because they carried no target value.
  std::uint32_t skipped_missing_hash = 0;
  // How many entries were dropped because they were malformed.
  std::uint32_t skipped_invalid = 0;
};

// The sampling geometry the target speaks for.
//
// The geometry of a canvas sample is a square, so the two sides are the
// same number; the geometry of a webgl sample is free.
FpSamplingSpec SamplingSpecOf(const FpTarget& target);

// Reads the fp_target section out of a frozen configuration bundle.
//
// A bundle without the section is not an error: it means the platform
// asked for no target, and the caller keeps the behaviour it had. Only a
// bundle that cannot be read at all fails; `*error` then says why.
//
// A malformed section fails the same way. An entry that is malformed is
// dropped and counted, and the entries around it are still read.
//
// An entry is malformed when its dimension is not one of the two, or its
// geometry does not describe a sample. An entry that is well formed but
// carries no target value is counted apart, because that is a different
// kind of problem: the entry says where it applies but not what to
// apply.
bool ParseFpTargetConfig(std::string_view bundle_json,
                         FpTargetTable* out,
                         std::string* error);

}  // namespace lengsuo::fp

#endif  // LENGSUO_NATIVE_FP_TARGET_H_
