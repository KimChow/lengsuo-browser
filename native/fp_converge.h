// fp_converge - the hamming distance and the convergence decision (task T3).
//
// The hamming distance answers one question: how far is the fingerprint a
// page actually produced from the fingerprint the platform asked for. The
// answer is only meaningful when the two are comparable, and comparability
// has two rules that this file never bends:
//
//   * the two strings must have the same length - the short one is never
//     padded to the longer one, because padding would invent a difference
//     that is not there;
//   * the two samples must have been taken at the same geometry - a
//     fingerprint of a 300x300 canvas and a fingerprint of a 64x64 canvas
//     are not two readings of the same thing.
//
// A third rule comes from the contract: the threshold is a configured
// value, never a constant in the source. Without a threshold there is no
// verdict at all.
//
// The convergence search walks a deterministic sequence of candidate
// perturbations and stops at the first one that is close enough. The
// sequence is a function of the target value and the geometry, so two runs
// over the same sample give the same answer.
#ifndef LENGSUO_NATIVE_FP_CONVERGE_H_
#define LENGSUO_NATIVE_FP_CONVERGE_H_

#include <cstdint>
#include <optional>
#include <span>
#include <string_view>

#include "fp_sampling.h"

namespace lengsuo::fp {

// The hamming distance between two strings.
//
// The distance is defined over strings of the same length. Strings of
// different length are not comparable and the function says so; the short
// one is never padded to the longer one.
std::optional<std::uint32_t> HammingDistance(std::string_view a,
                                              std::string_view b);

// The verdict of one comparison.
enum class FpConvergence : std::uint8_t {
  kPass,
  kFail,
  kNotComparable,
};

// Compares a sample against a target value.
//
// The comparison is refused when the strings have different lengths, when
// the two samples were taken at different geometries, or when no threshold
// was configured.
FpConvergence EvaluateConvergence(std::string_view actual,
                                   std::string_view expected,
                                   const FpSamplingSpec& actual_spec,
                                   const FpSamplingSpec& expected_spec,
                                   std::optional<std::uint32_t> similarity);

// The outcome of a convergence search.
struct FpConvergeResult {
  // True when a candidate was within the threshold.
  bool converged = false;
  // The seed of the candidate that was chosen.
  std::uint64_t seed = 0;
  // The distance of the chosen candidate to the target value.
  std::uint32_t distance = 0;
  // How many candidates were tried.
  std::uint32_t attempts = 0;
};

// Searches the deterministic candidate sequence for one whose fingerprint
// hash is within `similarity` of the target value.
//
// The candidates are a function of the target value and the geometry, so
// two runs over the same sample give the same answer. Without a threshold
// nothing is searched for. When no candidate is within the threshold the
// closest one is reported and `converged` stays false.
FpConvergeResult ConvergeToTarget(std::string_view target_hash,
                                   std::span<const std::uint8_t> src,
                                   std::uint64_t stride,
                                   const FpSamplingSpec& spec,
                                   std::optional<std::uint32_t> similarity,
                                   std::uint32_t max_attempts);

}  // namespace lengsuo::fp

#endif  // LENGSUO_NATIVE_FP_CONVERGE_H_
