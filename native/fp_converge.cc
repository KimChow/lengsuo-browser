#include "fp_converge.h"

#include <cstdint>
#include <limits>
#include <string>
#include <vector>

namespace lengsuo::fp {
namespace {

// The value that means "no candidate was seen yet".
constexpr std::uint32_t kNoDistance = std::numeric_limits<std::uint32_t>::max();

}  // namespace

std::optional<std::uint32_t> HammingDistance(std::string_view a,
                                              std::string_view b) {
  if (a.size() != b.size()) {
    // Not comparable. The short string is never padded to the long one.
    return std::nullopt;
  }
  std::uint32_t distance = 0;
  for (std::size_t index = 0; index < a.size(); ++index) {
    if (a[index] != b[index]) {
      ++distance;
    }
  }
  return distance;
}

FpConvergence EvaluateConvergence(std::string_view actual,
                                   std::string_view expected,
                                   const FpSamplingSpec& actual_spec,
                                   const FpSamplingSpec& expected_spec,
                                   std::optional<std::uint32_t> similarity) {
  if (!similarity.has_value()) {
    // No configured threshold: nothing can be judged.
    return FpConvergence::kNotComparable;
  }
  if (!SameSamplingSpec(actual_spec, expected_spec)) {
    // The two samples are not two readings of the same thing.
    return FpConvergence::kNotComparable;
  }
  const std::optional<std::uint32_t> distance =
      HammingDistance(actual, expected);
  if (!distance.has_value()) {
    return FpConvergence::kNotComparable;
  }
  return *distance <= *similarity ? FpConvergence::kPass
                                  : FpConvergence::kFail;
}

FpConvergeResult ConvergeToTarget(std::string_view target_hash,
                                   std::span<const std::uint8_t> src,
                                   std::uint64_t stride,
                                   const FpSamplingSpec& spec,
                                   std::optional<std::uint32_t> similarity,
                                   std::uint32_t max_attempts) {
  FpConvergeResult result;
  if (!similarity.has_value() || target_hash.empty()) {
    return result;
  }
  if (spec.width == 0 || spec.length == 0) {
    return result;
  }
  const std::uint64_t needed = stride * static_cast<std::uint64_t>(spec.length);
  if (needed == 0 || src.size() < needed) {
    return result;
  }

  std::vector<std::uint8_t> dst(static_cast<std::size_t>(needed));
  const std::uint64_t base = TargetSeed(target_hash, spec);
  std::uint32_t best_distance = kNoDistance;
  std::uint64_t best_seed = 0;

  for (std::uint32_t attempt = 0; attempt < max_attempts; ++attempt) {
    result.attempts = attempt + 1;
    const std::uint64_t seed = AttemptSeed(base, attempt);
    if (!ApplySeedNoise(seed, src, dst, spec.width, spec.length, stride)) {
      continue;
    }
    const std::string hash = FingerprintHashHex(dst, spec.width, spec.length);
    const std::optional<std::uint32_t> distance =
        HammingDistance(hash, target_hash);
    if (!distance.has_value()) {
      // The target has another length than the fingerprint this kernel
      // produces; the two are not comparable and the search has nothing
      // to search for.
      continue;
    }
    if (*distance < best_distance) {
      best_distance = *distance;
      best_seed = seed;
    }
    if (*distance <= *similarity) {
      result.converged = true;
      result.seed = seed;
      result.distance = *distance;
      return result;
    }
  }

  result.seed = best_seed;
  result.distance = best_distance == kNoDistance ? 0 : best_distance;
  return result;
}

}  // namespace lengsuo::fp
