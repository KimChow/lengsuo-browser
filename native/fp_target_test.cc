// fp_target_test - the target-value table and URL targeting (task T3).
//
// The brief's rulings each get a test of their own:
//
//   * an entry without target_hash drops out and is counted: the browser
//     keeps the existing behaviour and never crashes;
//   * several matching entries: the longest url_regex wins;
//   * no match in a dimension: no target, which is the existing behaviour;
//   * a target with skip:true matches but turns perturbation off.
//
// The allowed values are fixtures of this test file. Production code holds no
// target hash at all: everything comes from the ConfigBundle (contract 3).
#include "fp_target.h"

#include <gtest/gtest.h>

#include <string>

#include "fp_target_match.h"

namespace lengsuo::fp {
namespace {

// A bundle whose fp_target section carries `entries` verbatim.
std::string Bundle(const std::string& section_body) {
  return R"({"fp_target":{)" + section_body + "}}";
}

std::string TwoTargets() {
  return Bundle(
      R"("similarity":8,"targets":[)"
      R"({"dimension":"canvas","url_regex":"^https://a\\.example/.*",)"
      R"("target_hash":"0011223344556677","width":300,"length":300,)"
      R"("drew_text":true},)"
      R"({"dimension":"webgl","url_regex":"","target_hash":"8899aabbccddeeff",)"
      R"("width":64,"length":64}])");
}

// ---------------------------------------------------------------------------
// Parsing
// ---------------------------------------------------------------------------

TEST(FpTargetParse, ReadsEveryField) {
  FpTargetTable table;
  std::string error;
  ASSERT_TRUE(ParseFpTargetConfig(TwoTargets(), &table, &error)) << error;
  ASSERT_EQ(table.targets.size(), 2u);

  const FpTarget& canvas = table.targets[0];
  EXPECT_EQ(canvas.dimension, FpDimension::kCanvas);
  EXPECT_EQ(canvas.url_regex, "^https://a\\.example/.*");
  EXPECT_EQ(canvas.target_hash, "0011223344556677");
  EXPECT_EQ(canvas.width, 300u);
  EXPECT_EQ(canvas.length, 300u);
  EXPECT_TRUE(canvas.has_drew_text);
  EXPECT_TRUE(canvas.drew_text);
  EXPECT_FALSE(canvas.skip);

  const FpTarget& webgl = table.targets[1];
  EXPECT_EQ(webgl.dimension, FpDimension::kWebgl);
  EXPECT_TRUE(webgl.url_regex.empty());
  EXPECT_FALSE(webgl.has_drew_text);
  EXPECT_FALSE(webgl.drew_text);

  ASSERT_TRUE(table.similarity.has_value());
  EXPECT_EQ(*table.similarity, 8u);
}

TEST(FpTargetParse, SimilarityAbsentIsNoThreshold) {
  // Contract 3: thresholds come from the configuration. No configured
  // threshold means "cannot judge", never a constant baked into the code.
  FpTargetTable table;
  std::string error;
  ASSERT_TRUE(ParseFpTargetConfig(
      Bundle(R"("targets":[])"), &table, &error)) << error;
  EXPECT_FALSE(table.similarity.has_value());
  EXPECT_TRUE(table.targets.empty());
}

TEST(FpTargetParse, MissingTargetHashDropsTheEntry) {
  // The brief: a target value with no target_hash falls back to the existing
  // behaviour and must not crash.
  FpTargetTable table;
  std::string error;
  ASSERT_TRUE(ParseFpTargetConfig(
      Bundle(R"("targets":[{"dimension":"canvas","url_regex":"a",)"
             R"("width":10,"length":10}])"),
      &table, &error))
      << error;
  EXPECT_TRUE(table.targets.empty());
  EXPECT_EQ(table.skipped_missing_hash, 1u);
  EXPECT_EQ(table.skipped_invalid, 0u);
}

TEST(FpTargetParse, UnknownDimensionDropsTheEntry) {
  FpTargetTable table;
  std::string error;
  ASSERT_TRUE(ParseFpTargetConfig(
      Bundle(R"("targets":[{"dimension":"audio","target_hash":"ff",)"
             R"("width":10,"length":10}])"),
      &table, &error))
      << error;
  EXPECT_TRUE(table.targets.empty());
  EXPECT_EQ(table.skipped_invalid, 1u);
}

TEST(FpTargetParse, NonSquareGeometryDropsTheEntry) {
  FpTargetTable table;
  std::string error;
  ASSERT_TRUE(ParseFpTargetConfig(
      Bundle(R"("targets":[{"dimension":"canvas","target_hash":"ff",)"
             R"("width":300,"length":150}])"),
      &table, &error))
      << error;
  EXPECT_TRUE(table.targets.empty());
  EXPECT_EQ(table.skipped_invalid, 1u);
}

TEST(FpTargetParse, MalformedEntryDoesNotPoisonTheTable) {
  FpTargetTable table;
  std::string error;
  ASSERT_TRUE(ParseFpTargetConfig(
      Bundle(R"("targets":[{"dimension":"canvas"},)"
             R"({"dimension":"canvas","target_hash":"aa","width":8,)"
             R"("length":8}])"),
      &table, &error))
      << error;
  ASSERT_EQ(table.targets.size(), 1u);
  EXPECT_EQ(table.targets[0].target_hash, "aa");
  EXPECT_EQ(table.skipped_invalid, 1u);
}

TEST(FpTargetParse, AbsentSectionIsNotAnError) {
  // A bundle with no fp_target section means "no targeting": the existing
  // behaviour. It is not a malformed configuration.
  FpTargetTable table;
  std::string error;
  ASSERT_TRUE(ParseFpTargetConfig(R"({"profile_sync":{}})", &table, &error))
      << error;
  EXPECT_TRUE(table.targets.empty());
  EXPECT_FALSE(table.similarity.has_value());
}

TEST(FpTargetParse, MalformedJsonFails) {
  FpTargetTable table;
  std::string error;
  EXPECT_FALSE(ParseFpTargetConfig(R"({"fp_target":)", &table, &error));
  EXPECT_FALSE(error.empty());
}

TEST(FpTargetParse, TargetsMustBeAnArray) {
  FpTargetTable table;
  std::string error;
  EXPECT_FALSE(ParseFpTargetConfig(
      Bundle(R"("targets":{"dimension":"canvas"})"), &table, &error));
  EXPECT_FALSE(error.empty());
}

// ---------------------------------------------------------------------------
// Targeting
// ---------------------------------------------------------------------------

TEST(FpTargetMatch, LongestPatternWins) {
  // The brief: several matches resolve to the most specific one, and longer
  // url_regex means more specific. All three entries below speak for the
  // same URL, so the answer can only come from the length.
  FpTargetTable table;
  std::string error;
  ASSERT_TRUE(ParseFpTargetConfig(
      Bundle(R"("targets":[)"
             R"({"dimension":"canvas","url_regex":"^https://a\\.example/x",)"
             R"("target_hash":"11","width":8,"length":8},)"
             R"({"dimension":"canvas","url_regex":"^https://a\\.example/.*",)"
             R"("target_hash":"22","width":8,"length":8},)"
             R"({"dimension":"canvas","url_regex":"",)"
             R"("target_hash":"33","width":8,"length":8}])"),
      &table, &error))
      << error;

  const FpTarget* picked =
      SelectFpTarget(table, "https://a.example/x", FpDimension::kCanvas);
  ASSERT_NE(picked, nullptr);
  // "^https://a\.example/.*" is the longest of the three.
  EXPECT_EQ(picked->target_hash, "22");
}

TEST(FpTargetMatch, DimensionFiltersCandidates) {
  FpTargetTable table;
  std::string error;
  ASSERT_TRUE(ParseFpTargetConfig(TwoTargets(), &table, &error)) << error;

  // The webgl entry has an empty pattern (the dimension default), so it is
  // the only candidate for a webgl URL.
  const FpTarget* webgl =
      SelectFpTarget(table, "https://b.example/", FpDimension::kWebgl);
  ASSERT_NE(webgl, nullptr);
  EXPECT_EQ(webgl->dimension, FpDimension::kWebgl);

  // The canvas URL that matches nothing returns nothing: existing behaviour.
  EXPECT_EQ(SelectFpTarget(table, "https://b.example/", FpDimension::kCanvas),
            nullptr);
}

TEST(FpTargetMatch, EmptyPatternIsTheDimensionDefault) {
  FpTargetTable table;
  std::string error;
  ASSERT_TRUE(ParseFpTargetConfig(TwoTargets(), &table, &error)) << error;

  const FpTarget* picked =
      SelectFpTarget(table, "https://anything.example/", FpDimension::kWebgl);
  ASSERT_NE(picked, nullptr);
  EXPECT_TRUE(picked->url_regex.empty());
}

TEST(FpTargetMatch, SpecificPatternBeatsTheDefault) {
  FpTargetTable table;
  std::string error;
  ASSERT_TRUE(ParseFpTargetConfig(
      Bundle(R"("targets":[)"
             R"({"dimension":"canvas","url_regex":"","target_hash":"aa",)"
             R"("width":8,"length":8},)"
             R"({"dimension":"canvas","url_regex":"^https://x\\.example/",)"
             R"("target_hash":"bb","width":8,"length":8}])"),
      &table, &error))
      << error;

  const FpTarget* picked =
      SelectFpTarget(table, "https://x.example/", FpDimension::kCanvas);
  ASSERT_NE(picked, nullptr);
  EXPECT_EQ(picked->target_hash, "bb");
}

TEST(FpTargetMatch, NoTargetFallsBackToExistingBehaviour) {
  FpTargetTable table;  // empty: no fp_target section in the bundle
  const FpTargetDecision decision =
      DecideForUrl(table, "https://a.example/", FpDimension::kCanvas);
  EXPECT_FALSE(decision.apply);
  EXPECT_EQ(decision.target, nullptr);
}

TEST(FpTargetMatch, SkipTargetAppliesButPerturbsNothing) {
  FpTargetTable table;
  std::string error;
  ASSERT_TRUE(ParseFpTargetConfig(
      Bundle(R"("targets":[{"dimension":"canvas",)"
             R"("url_regex":"^https://s\\.example/",)"
             R"("target_hash":"aa","width":8,"length":8,"skip":true}])"),
      &table, &error))
      << error;

  const FpTargetDecision decision =
      DecideForUrl(table, "https://s.example/", FpDimension::kCanvas);
  EXPECT_TRUE(decision.apply);
  EXPECT_TRUE(decision.skip);
  ASSERT_NE(decision.target, nullptr);
}

// ---------------------------------------------------------------------------
// The url_regex subset
// ---------------------------------------------------------------------------

TEST(FpUrlRegex, MatchesLiteralsAndAnchors) {
  EXPECT_TRUE(MatchUrlRegex("^https://a.example/", "https://a.example/"));
  EXPECT_FALSE(MatchUrlRegex("^https://a.example/", "https://b.example/"));
}

TEST(FpUrlRegex, DotMatchesOneByte) {
  EXPECT_TRUE(MatchUrlRegex("a.c", "abc"));
  EXPECT_FALSE(MatchUrlRegex("a.c", "ac"));
  // A dot inside a class is a literal dot.
  EXPECT_TRUE(MatchUrlRegex("a[.]c", "a.c"));
  EXPECT_FALSE(MatchUrlRegex("a[.]c", "abc"));
}

TEST(FpUrlRegex, Quantifiers) {
  EXPECT_TRUE(MatchUrlRegex("ab*c", "ac"));
  EXPECT_TRUE(MatchUrlRegex("ab*c", "abbbc"));
  EXPECT_TRUE(MatchUrlRegex("ab+c", "abc"));
  EXPECT_FALSE(MatchUrlRegex("ab+c", "ac"));
  EXPECT_TRUE(MatchUrlRegex("ab?c", "ac"));
  EXPECT_TRUE(MatchUrlRegex("ab?c", "abc"));
  EXPECT_FALSE(MatchUrlRegex("ab?c", "abbc"));
}

TEST(FpUrlRegex, ClassesAndEscapes) {
  EXPECT_TRUE(MatchUrlRegex("[a-c]+", "abc"));
  EXPECT_FALSE(MatchUrlRegex("[a-c]+", "abd"));
  EXPECT_TRUE(MatchUrlRegex("[^a-c]+", "xyz"));
  EXPECT_FALSE(MatchUrlRegex("[^a-c]+", "xay"));
  EXPECT_TRUE(MatchUrlRegex(R"(https://\w+\.example/)", "https://host.example/"));
  EXPECT_TRUE(MatchUrlRegex(R"(\d+)", "12"));
  // The subset has no repetition counts, so a brace is a literal byte.
  EXPECT_TRUE(MatchUrlRegex(R"(\d{2})", "1{2}"));
  EXPECT_FALSE(MatchUrlRegex(R"(\d{2})", "12"));
}

TEST(FpUrlRegex, FullMatchOnly) {
  // Consistent with MatchGlob: the whole text must match.
  EXPECT_FALSE(MatchUrlRegex("abc", "xabc"));
  EXPECT_FALSE(MatchUrlRegex("abc", "abcx"));
  EXPECT_TRUE(MatchUrlRegex("abc", "abc"));
  EXPECT_TRUE(MatchUrlRegex("^abc$", "abc"));
}

TEST(FpUrlRegex, MalformedPatternNeverMatches) {
  // A pattern that cannot be parsed disables the target, which is the
  // specified fallback - it never aborts the renderer.
  EXPECT_FALSE(MatchUrlRegex("[a-", "a"));
  EXPECT_FALSE(MatchUrlRegex(R"(\)", "x"));
}

}  // namespace
}  // namespace lengsuo::fp
