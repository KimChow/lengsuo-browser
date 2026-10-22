// fp_sampling_test - the sampling geometry, the hash and the deterministic
// perturbation (task T3).
//
// The brief's rulings each get a test of their own:
//
//   * a canvas sample is taken at a square of a fixed side length;
//   * the sampling does not change the original pixel semantics: the source
//     buffer is left untouched and only the read-out copy is perturbed;
//   * the same target yields the same bytes, every time;
//   * a different target yields different bytes.
#include "fp_sampling.h"

#include <gtest/gtest.h>

#include <cstdint>
#include <span>
#include <string>
#include <vector>

namespace lengsuo::fp {
namespace {

// A 4x4 RGBA sample with a fixed pattern. The alpha channel is never 0, so
// every pixel takes part in the perturbation.
std::vector<std::uint8_t> SamplePixels() {
  std::vector<std::uint8_t> pixels(4 * 4 * 4);
  for (std::size_t i = 0; i < pixels.size(); ++i) {
    pixels[i] = static_cast<std::uint8_t>((i * 7 + 13) & 0xFF);
  }
  for (std::size_t i = 3; i < pixels.size(); i += 4) {
    pixels[i] = 255;
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

// ---------------------------------------------------------------------------
// The geometry
// ---------------------------------------------------------------------------

TEST(FpSampling, SquareSideIsTheWidth) {
  FpSamplingSpec spec;
  spec.width = 300;
  spec.length = 300;
  EXPECT_EQ(SamplingSideLength(spec), 300u);
}

TEST(FpSampling, NonSquareGeometryHasNoSide) {
  FpSamplingSpec spec;
  spec.width = 300;
  spec.length = 150;
  EXPECT_EQ(SamplingSideLength(spec), 0u);
}

TEST(FpSampling, ZeroGeometryHasNoSide) {
  FpSamplingSpec spec;
  EXPECT_EQ(SamplingSideLength(spec), 0u);
}

TEST(FpSampling, SameSpecIsEqual) {
  const FpSamplingSpec a = Spec();
  FpSamplingSpec b = Spec();
  EXPECT_TRUE(SameSamplingSpec(a, b));

  b.width = 8;
  EXPECT_FALSE(SameSamplingSpec(a, b));
}

TEST(FpSampling, DifferentDrewTextIsADifferentSpec) {
  const FpSamplingSpec a = Spec();
  FpSamplingSpec b = Spec();
  b.drew_text = std::optional<bool>(false);
  EXPECT_FALSE(SameSamplingSpec(a, b));
}

TEST(FpSampling, AnUnconstrainedDrewTextMatchesAnything) {
  const FpSamplingSpec a = Spec();
  FpSamplingSpec b = Spec();
  b.drew_text = std::nullopt;
  EXPECT_TRUE(SameSamplingSpec(a, b));
}

// ---------------------------------------------------------------------------
// The hash
// ---------------------------------------------------------------------------

TEST(FpHash, IsDeterministic) {
  const std::vector<std::uint8_t> pixels = SamplePixels();
  EXPECT_EQ(FingerprintHash(pixels, 4, 4),
            FingerprintHash(pixels, 4, 4));
  EXPECT_EQ(FingerprintHashHex(pixels, 4, 4),
            FingerprintHashHex(pixels, 4, 4));
}

TEST(FpHash, DifferentPixelsHashDifferently) {
  std::vector<std::uint8_t> pixels = SamplePixels();
  const std::uint64_t before = FingerprintHash(pixels, 4, 4);
  pixels[0] = static_cast<std::uint8_t>(pixels[0] ^ 0xFF);
  EXPECT_NE(FingerprintHash(pixels, 4, 4), before);
}

TEST(FpHash, HexIsSixteenLowercaseCharacters) {
  const std::vector<std::uint8_t> pixels = SamplePixels();
  const std::string hex = FingerprintHashHex(pixels, 4, 4);
  ASSERT_EQ(hex.size(), 16u);
  for (const char c : hex) {
    EXPECT_TRUE((c >= '0' && c <= '9') || (c >= 'a' && c <= 'f')) << c;
  }
}

// ---------------------------------------------------------------------------
// The seed
// ---------------------------------------------------------------------------

TEST(FpSeed, TheSameTargetGivesTheSameSeed) {
  const FpSamplingSpec spec = Spec();
  EXPECT_EQ(TargetSeed("abcdef", spec), TargetSeed("abcdef", spec));
}

TEST(FpSeed, ADifferentTargetGivesADifferentSeed) {
  const FpSamplingSpec spec = Spec();
  EXPECT_NE(TargetSeed("abcdef", spec), TargetSeed("abcdeg", spec));
}

TEST(FpSeed, ADifferentGeometryGivesADifferentSeed) {
  FpSamplingSpec other = Spec();
  other.width = 8;
  other.length = 8;
  EXPECT_NE(TargetSeed("abcdef", Spec()), TargetSeed("abcdef", other));
}

TEST(FpSeed, AttemptsGiveDifferentSeeds) {
  const std::uint64_t base = TargetSeed("abcdef", Spec());
  EXPECT_NE(AttemptSeed(base, 0), AttemptSeed(base, 1));
  EXPECT_EQ(AttemptSeed(base, 2), AttemptSeed(base, 2));
}

// ---------------------------------------------------------------------------
// The perturbation
// ---------------------------------------------------------------------------

TEST(FpNoise, SameTargetGivesTheSameBytes) {
  // The evidence the brief asks for: one target value run twice gives
  // byte-identical output.
  const std::vector<std::uint8_t> src = SamplePixels();
  const FpSamplingSpec spec = Spec();

  std::vector<std::uint8_t> first(src.size());
  std::vector<std::uint8_t> second(src.size());
  ASSERT_TRUE(ApplyTargetNoise("abcdef", spec, src, first, 4, 4, 16));
  ASSERT_TRUE(ApplyTargetNoise("abcdef", spec, src, second, 4, 4, 16));
  EXPECT_EQ(first, second);
}

TEST(FpNoise, ADifferentTargetGivesDifferentBytes) {
  const std::vector<std::uint8_t> src = SamplePixels();
  const FpSamplingSpec spec = Spec();

  std::vector<std::uint8_t> first(src.size());
  std::vector<std::uint8_t> second(src.size());
  ASSERT_TRUE(ApplyTargetNoise("abcdef", spec, src, first, 4, 4, 16));
  ASSERT_TRUE(ApplyTargetNoise("abcdeg", spec, src, second, 4, 4, 16));
  EXPECT_NE(first, second);
}

TEST(FpNoise, TheSourceIsNotTouched) {
  // The sampling must not change the original pixels: only the read-out
  // copy carries the perturbation.
  const std::vector<std::uint8_t> src = SamplePixels();
  const std::vector<std::uint8_t> before = src;
  std::vector<std::uint8_t> dst(src.size());
  ASSERT_TRUE(ApplyTargetNoise("abcdef", Spec(), src, dst, 4, 4, 16));
  EXPECT_EQ(src, before);
}

TEST(FpNoise, TransparentPixelsArePreserved) {
  std::vector<std::uint8_t> src = SamplePixels();
  for (std::size_t i = 3; i < src.size(); i += 4) {
    src[i] = 0;
  }
  const std::vector<std::uint8_t> before = src;
  std::vector<std::uint8_t> dst(src.size());
  ASSERT_TRUE(ApplyTargetNoise("abcdef", Spec(), src, dst, 4, 4, 16));
  EXPECT_EQ(dst, before);
}

TEST(FpNoise, RejectsATooSmallBuffer) {
  const std::vector<std::uint8_t> src = SamplePixels();
  std::vector<std::uint8_t> dst(4);
  EXPECT_FALSE(ApplyTargetNoise("abcdef", Spec(), src, dst, 4, 4, 16));
}

TEST(FpNoise, RejectsAnEmptyGeometry) {
  const std::vector<std::uint8_t> src = SamplePixels();
  std::vector<std::uint8_t> dst(src.size());
  EXPECT_FALSE(ApplyTargetNoise("abcdef", Spec(), src, dst, 0, 0, 0));
}

}  // namespace
}  // namespace lengsuo::fp
