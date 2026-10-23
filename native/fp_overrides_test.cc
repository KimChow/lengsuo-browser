// fp_overrides_test - the per-URL target value entry (task 0019).
//
// The three questions the card asks of this entry:
//
//   * no target governs the page  => the value the renderer produced before
//     this entry existed, byte for byte,
//   * the same target and the same sampling geometry => the same bytes,
//   * several targets match one URL => the most specific one wins.
//
// The tests drive the entry directly, because nothing in the tree calls it
// yet; see docs/l3-reports/task-T3-wiring.md for the driver question.
#include "fp_overrides.h"

#include <cstdint>
#include <string>
#include <string_view>
#include <vector>

#include "fp_sampling.h"
#include "fp_target.h"
#include "fp_target_match.h"
#include "gtest/gtest.h"

namespace lengsuo::fp {
namespace {

// A profile with the webgl slot ON (field 7 is the slot, field 0 the seed).
constexpr char kProfileWebglOn[] =
    "1234abcd:0:0:0:0:0:0:7:Vendor:Renderer:8:16";
// The same profile with the webgl slot OFF.
constexpr char kProfileWebglOff[] =
    "1234abcd:0:0:0:0:0:0:0:Vendor:Renderer:8:16";

// A bundle with a canvas default, a webgl entry for one host and a webgl
// catch-all that says "do not perturb".
constexpr char kBundle[] = R"({
  "fp_target": {
    "similarity": 8,
    "targets": [
      {"dimension": "canvas", "target_hash": "aa11", "width": 64,
       "length": 64},
      {"dimension": "webgl", "url_regex": "^https://a\\.test/.*",
       "target_hash": "bb22", "width": 128, "length": 128},
      {"dimension": "webgl", "url_regex": ".*", "target_hash": "cc33",
       "width": 128, "length": 128, "skip": true}
    ]
  }
})";

constexpr char kBundleCanvasOnly[] = R"({
  "fp_target": {"targets": [
    {"dimension": "canvas", "target_hash": "aa11", "width": 32,
     "length": 32}
  ]}
})";

constexpr char kBundleNoSection[] = R"({"some_other_section": {"x": 1}})";

// The capability caps this dimension is allowed to touch, and a spread of
// driver values to run them against.
constexpr std::uint32_t kCapPnames[] = {
    0x0D33, 0x8073, 0x84E8, 0x851C, 0x8869, 0x8872, 0x88FF,
    0x8B4C, 0x8B4D, 0x8D57, 0x8D6B, 0x8DFB, 0x8DFC, 0x8DFD,
};
constexpr std::int32_t kDriverValues[] = {
    1,   2,    3,    7,    16,   24,  31,
    32,  63,   64,   96,   128,  256, 512,
    1024, 2048, 4096, 16384,
};

constexpr std::string_view kGovernedUrl = "https://a.test/page";
constexpr std::string_view kUngovernedUrl = "https://other.test/page";

// A buffer of RGBA8 pixels at the geometry of the webgl target above.
std::vector<std::uint8_t> PixelBuffer(std::uint32_t side) {
  std::vector<std::uint8_t> pixels(
      static_cast<std::size_t>(side) * side * 4);
  for (std::size_t i = 0; i < pixels.size(); ++i) {
    pixels[i] = static_cast<std::uint8_t>((i * 37 + 11) % 251);
  }
  return pixels;
}

TEST(FpOverridesTarget, NoSectionInstallsNothing) {
  InitFromSwitch(kProfileWebglOn);
  ASSERT_TRUE(SetFpTargetsFromBundle(kBundleNoSection));

  EXPECT_FALSE(HasFpTargets(FpDimension::kCanvas));
  EXPECT_FALSE(HasFpTargets(FpDimension::kWebgl));
  EXPECT_EQ(FpTargetForUrl(kGovernedUrl, FpDimension::kCanvas), nullptr);
  EXPECT_EQ(FpTargetForUrl(kGovernedUrl, FpDimension::kWebgl), nullptr);
  std::uint64_t seed = 7;
  EXPECT_FALSE(FpTargetSeedForUrl(kGovernedUrl, FpDimension::kCanvas, &seed));
  EXPECT_EQ(seed, 7u);
}

// The regression guard: with no target governing the page, the URL-aware exit
// is the existing profile exit, value for value.
TEST(FpOverridesTarget, NoTargetIsByteIdenticalToProfileBias) {
  InitFromSwitch(kProfileWebglOn);
  ASSERT_TRUE(SetFpTargetsFromBundle(kBundleNoSection));

  for (const std::uint32_t pname : kCapPnames) {
    for (const std::int32_t original : kDriverValues) {
      const std::int32_t profile = WebGLParamBias(pname, original);
      EXPECT_EQ(WebGLParamBiasForUrl(kGovernedUrl, pname, original), profile)
          << "pname=" << pname << " original=" << original;
      EXPECT_EQ(WebGLParamBiasForUrl(kUngovernedUrl, pname, original), profile);
      EXPECT_EQ(WebGLParamBiasForUrl(std::string_view(), pname, original),
                profile);
    }
  }
}

// A malformed bundle must never install half a table, and must never keep the
// previous one.
TEST(FpOverridesTarget, MalformedBundleFailsClosed) {
  ASSERT_TRUE(SetFpTargetsFromBundle(kBundle));
  ASSERT_NE(FpTargetForUrl(kGovernedUrl, FpDimension::kWebgl), nullptr);

  EXPECT_FALSE(SetFpTargetsFromBundle("{not json"));
  EXPECT_EQ(FpTargetForUrl(kGovernedUrl, FpDimension::kWebgl), nullptr);
  EXPECT_FALSE(HasFpTargets(FpDimension::kWebgl));

  // A section that is present but not an object is malformed too.
  EXPECT_FALSE(SetFpTargetsFromBundle(R"({"fp_target": 5})"));
  EXPECT_FALSE(HasFpTargets(FpDimension::kWebgl));

  // A malformed entry inside a readable section is dropped, and it does not
  // take the entries around it down.
  ASSERT_TRUE(SetFpTargetsFromBundle(R"({"fp_target": {"targets": [
    {"dimension": "webgl", "url_regex": ".*", "target_hash": "dd44",
     "width": 8, "length": 8},
    {"dimension": "webgl", "url_regex": ".*"}
  ]}})"));
  const FpTarget* target = FpTargetForUrl(kUngovernedUrl, FpDimension::kWebgl);
  ASSERT_NE(target, nullptr);
  EXPECT_EQ(target->target_hash, "dd44");
}

// The most specific pattern wins; a target whose pattern is empty is the
// default of its dimension.
TEST(FpOverridesTarget, MostSpecificTargetWins) {
  ASSERT_TRUE(SetFpTargetsFromBundle(kBundle));

  const FpTarget* governed = FpTargetForUrl(kGovernedUrl, FpDimension::kWebgl);
  ASSERT_NE(governed, nullptr);
  EXPECT_EQ(governed->target_hash, "bb22");
  EXPECT_FALSE(governed->skip);

  const FpTarget* ungoverned =
      FpTargetForUrl(kUngovernedUrl, FpDimension::kWebgl);
  ASSERT_NE(ungoverned, nullptr);
  EXPECT_EQ(ungoverned->target_hash, "cc33");
  EXPECT_TRUE(ungoverned->skip);

  // The canvas entry carries no pattern at all, so it is the canvas default
  // and matches every URL of that dimension.
  const FpTarget* canvas = FpTargetForUrl(kUngovernedUrl, FpDimension::kCanvas);
  ASSERT_NE(canvas, nullptr);
  EXPECT_EQ(canvas->target_hash, "aa11");
}

// A target that says "do not perturb" leaves the profile value untouched.
TEST(FpOverridesTarget, SkippedTargetKeepsProfileValue) {
  InitFromSwitch(kProfileWebglOn);
  ASSERT_TRUE(SetFpTargetsFromBundle(kBundle));

  std::uint64_t seed = 0;
  EXPECT_FALSE(FpTargetSeedForUrl(kUngovernedUrl, FpDimension::kWebgl, &seed));
  for (const std::uint32_t pname : kCapPnames) {
    for (const std::int32_t original : kDriverValues) {
      EXPECT_EQ(WebGLParamBiasForUrl(kUngovernedUrl, pname, original),
                WebGLParamBias(pname, original));
    }
  }
}

// The same target at the same geometry is the same bytes: that is the property
// the whole convergence loop rests on.
TEST(FpOverridesTarget, SameTargetAndGeometryGiveSameBytes) {
  ASSERT_TRUE(SetFpTargetsFromBundle(kBundle));

  std::uint64_t first = 0;
  std::uint64_t second = 0;
  ASSERT_TRUE(FpTargetSeedForUrl(kGovernedUrl, FpDimension::kWebgl, &first));
  ASSERT_TRUE(FpTargetSeedForUrl(kGovernedUrl, FpDimension::kWebgl, &second));
  EXPECT_EQ(first, second);

  constexpr std::uint32_t kSide = 128;
  constexpr std::uint64_t kStride = kSide * 4;
  const std::vector<std::uint8_t> src = PixelBuffer(kSide);
  std::vector<std::uint8_t> a(src.size());
  std::vector<std::uint8_t> b(src.size());
  std::vector<std::uint8_t> c(src.size());
  ASSERT_TRUE(
      ApplySeedNoise(first, src, a, kSide, kSide, kStride));
  ASSERT_TRUE(
      ApplySeedNoise(second, src, b, kSide, kSide, kStride));
  EXPECT_EQ(a, b);

  // And it is the seed the target pipeline itself uses.
  FpSamplingSpec spec;
  spec.width = kSide;
  spec.length = kSide;
  const FpTarget* target = FpTargetForUrl(kGovernedUrl, FpDimension::kWebgl);
  ASSERT_NE(target, nullptr);
  EXPECT_TRUE(SameSamplingSpec(spec, SamplingSpecOf(*target)));
  ASSERT_TRUE(ApplyTargetNoise(target->target_hash, spec, src, c, kSide, kSide,
                               kStride));
  EXPECT_EQ(a, c);
}

// No page URL means no target, even when a default entry would match anything.
TEST(FpOverridesTarget, EmptyUrlNeverMatches) {
  InitFromSwitch(kProfileWebglOn);
  ASSERT_TRUE(SetFpTargetsFromBundle(kBundle));

  EXPECT_EQ(FpTargetForUrl(std::string_view(), FpDimension::kCanvas), nullptr);
  EXPECT_EQ(FpTargetForUrl(std::string_view(), FpDimension::kWebgl), nullptr);
  std::uint64_t seed = 0;
  EXPECT_FALSE(FpTargetSeedForUrl(std::string_view(), FpDimension::kCanvas,
                                  &seed));
  EXPECT_EQ(WebGLParamBiasForUrl(std::string_view(), 0x0D33, 4096),
            WebGLParamBias(0x0D33, 4096));
}

// The dimensions do not leak into each other.
TEST(FpOverridesTarget, DimensionsAreSeparate) {
  ASSERT_TRUE(SetFpTargetsFromBundle(kBundleCanvasOnly));

  EXPECT_TRUE(HasFpTargets(FpDimension::kCanvas));
  EXPECT_FALSE(HasFpTargets(FpDimension::kWebgl));
  EXPECT_NE(FpTargetForUrl(kGovernedUrl, FpDimension::kCanvas), nullptr);
  EXPECT_EQ(FpTargetForUrl(kGovernedUrl, FpDimension::kWebgl), nullptr);

  std::uint64_t seed = 0;
  EXPECT_FALSE(FpTargetSeedForUrl(kGovernedUrl, FpDimension::kWebgl, &seed));
  EXPECT_TRUE(FpTargetSeedForUrl(kGovernedUrl, FpDimension::kCanvas, &seed));
}

// A governing target drives the value even when the profile left that
// dimension native, and the value it reports is still a plausible cap: present,
// and never above what the driver said.
TEST(FpOverridesTarget, TargetGovernsWhenProfileSlotIsOff) {
  InitFromSwitch(kProfileWebglOff);
  ASSERT_TRUE(SetFpTargetsFromBundle(kBundle));
  ASSERT_EQ(WebGLParamBias(0x0D33, 4096), 4096) << "slot must be off";

  bool moved = false;
  for (const std::uint32_t pname : kCapPnames) {
    for (const std::int32_t original : kDriverValues) {
      const std::int32_t value =
          WebGLParamBiasForUrl(kGovernedUrl, pname, original);
      ASSERT_GT(value, 0);
      ASSERT_LE(value, original);
      if (value != original) {
        moved = true;
      }
    }
  }
  EXPECT_TRUE(moved) << "the target seed never reached the cap selection";
}

}  // namespace
}  // namespace lengsuo::fp