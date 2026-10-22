// fp_converge_test - hamming distance and the convergence decision (task T3).
//
// The brief's rulings each get a test of their own:
//
//   * hamming distance over equal-length strings;
//   * strings of different length are NOT comparable - the short one is
//     never padded;
//   * distance <= threshold passes, otherwise it fails;
//   * a sample whose geometry differs from the target's is not comparable,
//     and no distance is computed for it;
//   * the convergence search is deterministic and finds the candidate that
//     matches the target.
#include "fp_converge.h"

#include <gtest/gtest.h>

#include <cstdint>
#include <optional>
#include <span>
#include <string>
#include <vector>

#include "fp_sampling.h"

namespace lengsuo::fp {
namespace {

// A 4x4 RGBA sample with a fixed pattern.
std::vector<std::uint8_t> SamplePixels() {
  std::vector<std::uint8_t> pixels(4 * 4 * 4);
  for (std::size_t i = 0; i < pixels.size(); ++i) {
    pixels[i] = static_cast<std::uint8_t>((i * 7 + 13) & 0xFF);
  }
  return pixels;
}

FpSamplingSpec Spec() {
  FpSamplingSpec spec;
  spec.width = 4;
  spec.length = 4;
  spec.drew_text = std::optional<bool>(true);
  return spec;
}

// The hex hash of one candidate of the search.
std::string CandidateHash(std::string_view target_hash,
                          const FpSamplingSpec& spec,
                          std::uint32_t attempt) {
  const std::vector<std::uint8_t> src = SamplePixels();
  std::vector<std::uint8_t> dst(src.size());
  const std::uint64_t seed =
      AttemptSeed(TargetSeed(target_hash, spec), attempt);
  EXPECT_TRUE(ApplySeedNoise(seed, src, dst, spec.width, spec.length,
                             spec.width * 4));
  return FingerprintHashHex(dst, spec.width, spec.length);
}

// ---------------------------------------------------------------------------
// Hamming distance
// ---------------------------------------------------------------------------

TEST(FpHamming, CountsDifferingPositions) {
  const auto distance = HammingDistance("abcd", "abed");
  ASSERT_TRUE(distance.has_value());
  EXPECT_EQ(*distance, 1u);

  const auto none = HammingDistance("abcd", "abcd");
  ASSERT_TRUE(none.has_value());
  EXPECT_EQ(*none, 0u);

  const auto two = HammingDistance("abcd", "wxyz");
  ASSERT_TRUE(two.has_value());
  EXPECT_EQ(*two, 4u);
}

TEST(FpHamming, DifferentLengthIsNotComparable) {
  // The brief: the distance between strings of different length is
  // undefined. The short one is never padded to the longer one.
  EXPECT_FALSE(HammingDistance("abc", "abcd").has_value());
  EXPECT_FALSE(HammingDistance("abcd", "abc").has_value());
  EXPECT_FALSE(HammingDistance("", "a").has_value());
  EXPECT_TRUE(HammingDistance("", "").has_value());
}

TEST(FpHamming, EmptyStringsAreZeroApart) {
  const auto distance = HammingDistance("", "");
  ASSERT_TRUE(distance.has_value());
  EXPECT_EQ(*distance, 0u);
}

// ---------------------------------------------------------------------------
// The verdict
// ---------------------------------------------------------------------------

TEST(FpConvergence, DistanceAtOrBelowThresholdPasses) {
  const FpSamplingSpec spec = Spec();
  EXPECT_EQ(EvaluateConvergence("abcd", "abed", spec, spec, 1),
            FpConvergence::kPass);
  EXPECT_EQ(EvaluateConvergence("abcd", "abed", spec, spec, 2),
            FpConvergence::kPass);
}

TEST(FpConvergence, DistanceAboveThresholdFails) {
  const FpSamplingSpec spec = Spec();
  EXPECT_EQ(EvaluateConvergence("abcd", "wxyz", spec, spec, 3),
            FpConvergence::kFail);
  EXPECT_EQ(EvaluateConvergence("abcd", "wxyz", spec, spec, 0),
            FpConvergence::kFail);
}

TEST(FpConvergence, DifferentLengthIsNotComparable) {
  const FpSamplingSpec spec = Spec();
  EXPECT_EQ(EvaluateConvergence("abc", "abcd", spec, spec, 8),
            FpConvergence::kNotComparable);
}

TEST(FpConvergence, DifferentGeometryIsNotComparable) {
  // No distance is computed for a sample that was taken at another size.
  const FpSamplingSpec actual = Spec();
  FpSamplingSpec expected = Spec();
  expected.width = 8;
  EXPECT_EQ(EvaluateConvergence("abcd", "abed", actual, expected, 8),
            FpConvergence::kNotComparable);

  FpSamplingSpec other_length = Spec();
  other_length.length = 8;
  EXPECT_EQ(EvaluateConvergence("abcd", "abed", actual, other_length, 8),
            FpConvergence::kNotComparable);
}

TEST(FpConvergence, DifferentDrewTextIsNotComparable) {
  const FpSamplingSpec actual = Spec();
  FpSamplingSpec expected = Spec();
  expected.drew_text = std::optional<bool>(false);
  EXPECT_EQ(EvaluateConvergence("abcd", "abed", actual, expected, 8),
            FpConvergence::kNotComparable);
}

TEST(FpConvergence, AnUnconstrainedDrewTextIsComparable) {
  // The target did not say whether text was drawn, so the distance is
  // meaningful even though the sample did draw text.
  const FpSamplingSpec actual = Spec();
  FpSamplingSpec expected = Spec();
  expected.drew_text = std::nullopt;
  EXPECT_EQ(EvaluateConvergence("abcd", "abed", actual, expected, 8),
            FpConvergence::kPass);
}

TEST(FpConvergence, WithoutAThresholdNothingCanBeJudged) {
  // Contract 3: the threshold comes from the configuration. Without one
  // there is no verdict - the kernel does not carry a constant.
  const FpSamplingSpec spec = Spec();
  EXPECT_EQ(EvaluateConvergence("abcd", "abed", spec, spec, std::nullopt),
            FpConvergence::kNotComparable);
}

// ---------------------------------------------------------------------------
// The convergence search
// ---------------------------------------------------------------------------

TEST(FpConverge, FindsTheCandidateThatMatchesTheTarget) {
  const FpSamplingSpec spec = Spec();
  const std::string target = "0123456789abcdef";
  const std::vector<std::uint8_t> src = SamplePixels();

  // The best candidate of the deterministic search, computed the same way
  // the search computes it.
  std::uint32_t best_distance = 0xFFFFFFFFu;
  for (std::uint32_t attempt = 0; attempt < 8; ++attempt) {
    const std::string hash = CandidateHash(target, spec, attempt);
    const auto distance = HammingDistance(hash, target);
    ASSERT_TRUE(distance.has_value());
    if (*distance < best_distance) {
      best_distance = *distance;
    }
  }

  const FpConvergeResult result =
      ConvergeToTarget(target, src, spec.width * 4, spec, best_distance, 8);
  EXPECT_TRUE(result.converged);
  EXPECT_LE(result.distance, best_distance);
  EXPECT_LE(result.attempts, 8u);
  // The seed that was chosen is one of the candidates of the search.
  bool is_candidate = false;
  for (std::uint32_t attempt = 0; attempt < 8; ++attempt) {
    if (result.seed == AttemptSeed(TargetSeed(target, spec), attempt)) {
      is_candidate = true;
    }
  }
  EXPECT_TRUE(is_candidate);
}

TEST(FpConverge, IsDeterministic) {
  const FpSamplingSpec spec = Spec();
  const std::string target = "0123456789abcdef";
  const std::vector<std::uint8_t> src = SamplePixels();

  const FpConvergeResult first =
      ConvergeToTarget(target, src, spec.width * 4, spec, 8, 8);
  const FpConvergeResult second =
      ConvergeToTarget(target, src, spec.width * 4, spec, 8, 8);
  EXPECT_EQ(first.converged, second.converged);
  EXPECT_EQ(first.seed, second.seed);
  EXPECT_EQ(first.distance, second.distance);
  EXPECT_EQ(first.attempts, second.attempts);
}

TEST(FpConverge, WithoutAThresholdNothingConverges) {
  const FpSamplingSpec spec = Spec();
  const std::vector<std::uint8_t> src = SamplePixels();
  const FpConvergeResult result =
      ConvergeToTarget("0123456789abcdef", src, spec.width * 4, spec,
                       std::nullopt, 8);
  EXPECT_FALSE(result.converged);
  EXPECT_EQ(result.attempts, 0u);
}

TEST(FpConverge, ATargetOfAnotherLengthNeverConverges) {
  // The target hash and the produced hash have different lengths, so they
  // are not comparable and the search has nothing to search for.
  const FpSamplingSpec spec = Spec();
  const std::vector<std::uint8_t> src = SamplePixels();
  const FpConvergeResult result =
      ConvergeToTarget("0123456789abcdef0123456789abcdef", src,
                       spec.width * 4, spec, 8, 8);
  EXPECT_FALSE(result.converged);
}

TEST(FpConverge, TheClosestCandidateIsReportedWhenNoneIsWithinRange) {
  const FpSamplingSpec spec = Spec();
  const std::string target = "0123456789abcdef";
  const std::vector<std::uint8_t> src = SamplePixels();
  const FpConvergeResult result =
      ConvergeToTarget(target, src, spec.width * 4, spec, 0, 4);
  // With a threshold of zero only an exact match passes. The search still
  // reports the closest candidate it saw.
  EXPECT_FALSE(result.converged);
  EXPECT_EQ(result.attempts, 4u);
  EXPECT_LE(result.distance, 16u);
}

}  // namespace
}  // namespace lengsuo::fp
