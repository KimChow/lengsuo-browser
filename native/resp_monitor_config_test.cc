// resp_monitor_config_test - the rules of the response monitor (task T1).
//
// The rules come from the configuration, so the tests are about what the
// configuration is allowed to say and what happens when it says something
// unusable. The rule the tests hold onto hardest: one broken rule must not
// take the rest of the configuration down with it.
#include "resp_monitor_config.h"

#include <gtest/gtest.h>

#include <string>

namespace lengsuo::fp {
namespace {

// The section the platform sends, with one good rule.
const char kGoodSection[] = R"({
  "enabled": true,
  "rules": [
    {"id": "r1", "url_regex": "^https://example\\.test/a", "max_count": 2}
  ]
})";

// Parses a section and hands back the config. The section is known to parse,
// so a failure here is a bug in the test.
RespMonitorConfig ParseOk(const std::string& section) {
  RespMonitorConfig config;
  std::string error;
  EXPECT_TRUE(ParseRespMonitorConfig(section, &config, &error)) << error;
  return config;
}

TEST(RespMonitorConfig, ParsesAWholeRule) {
  const RespMonitorConfig config = ParseOk(kGoodSection);
  EXPECT_TRUE(config.enabled);
  ASSERT_EQ(config.rules.size(), 1u);
  const RespMonitorRule& rule = config.rules[0];
  EXPECT_EQ(rule.id, "r1");
  EXPECT_EQ(rule.url_regex, "^https://example\\.test/a");
  EXPECT_EQ(rule.max_count, 2u);
  EXPECT_TRUE(IsValidRespMonitorUrlRegex(rule.url_regex));
  // A rule that says nothing about the content type matches every one.
  EXPECT_TRUE(rule.content_type.empty());
  // The body is not carried unless the configuration asks for it.
  EXPECT_FALSE(rule.include_body);
}

TEST(RespMonitorConfig, TheWholeFeatureIsOffByDefault) {
  RespMonitorConfig config;
  std::string error;
  // No "enabled" at all means off: a section that does not say it is on is
  // a section that is not on.
  EXPECT_TRUE(ParseRespMonitorConfig(R"({"rules":[]})", &config, &error));
  EXPECT_FALSE(config.enabled);

  EXPECT_TRUE(
      ParseRespMonitorConfig(R"({"enabled":false,"rules":[]})", &config, &error));
  EXPECT_FALSE(config.enabled);
}

TEST(RespMonitorConfig, AnInvalidRegexSkipsThatRuleAndCountsIt) {
  const RespMonitorConfig config = ParseOk(R"({
    "enabled": true,
    "rules": [
      {"id": "broken", "url_regex": "([unclosed", "max_count": 1},
      {"id": "good", "url_regex": "^https://ok\\.test/", "max_count": 1}
    ]
  })");
  // The broken rule is dropped; the good one is still there.
  ASSERT_EQ(config.rules.size(), 1u);
  EXPECT_EQ(config.rules[0].id, "good");
  // And the drop is counted, so the operator can see it.
  EXPECT_EQ(config.skipped_rules, 1u);
}

TEST(RespMonitorConfig, ABrokenRuleDoesNotFailTheWholeConfig) {
  // Every rule broken: the config still parses, and it holds no rules.
  const RespMonitorConfig config = ParseOk(R"({
    "enabled": true,
    "rules": [
      {"id": "a", "url_regex": "([unclosed"},
      {"id": "b", "url_regex": "*bad"}
    ]
  })");
  EXPECT_TRUE(config.enabled);
  EXPECT_TRUE(config.rules.empty());
  EXPECT_EQ(config.skipped_rules, 2u);
}

TEST(RespMonitorConfig, ARuleWithoutAnIdOrAPatternIsSkipped) {
  const RespMonitorConfig config = ParseOk(R"({
    "enabled": true,
    "rules": [
      {"url_regex": "^https://example\\.test/"},
      {"id": "no-pattern"},
      {"id": "", "url_regex": "^https://example\\.test/"}
    ]
  })");
  EXPECT_TRUE(config.rules.empty());
  EXPECT_EQ(config.skipped_rules, 3u);
}

TEST(RespMonitorConfig, ADisabledRuleIsCountedApartFromASkippedOne) {
  const RespMonitorConfig config = ParseOk(R"({
    "enabled": true,
    "rules": [
      {"id": "off", "url_regex": "^https://example\\.test/", "enabled": false},
      {"id": "broken", "url_regex": "([unclosed"}
    ]
  })");
  EXPECT_TRUE(config.rules.empty());
  // A rule that said it was off is a decision, not a mistake.
  EXPECT_EQ(config.disabled_rules, 1u);
  EXPECT_EQ(config.skipped_rules, 1u);
}

TEST(RespMonitorConfig, TheContentTypeIsNormalized) {
  const RespMonitorConfig config = ParseOk(R"({
    "enabled": true,
    "rules": [
      {"id": "r1", "url_regex": "^https://example\\.test/", "content_type":
       "TEXT/Plain; charset=utf-8"}
    ]
  })");
  ASSERT_EQ(config.rules.size(), 1u);
  EXPECT_EQ(config.rules[0].content_type, "text/plain");
}

TEST(RespMonitorConfig, TheBodyIsCarriedOnlyWhenItIsAskedFor) {
  RespMonitorConfig config;
  std::string error;
  EXPECT_TRUE(ParseRespMonitorConfig(
      R"({"enabled":true,"include_body":true,"rules":[
           {"id":"with","url_regex":"a","include_body":true},
           {"id":"without","url_regex":"b"}]})",
      &config, &error));
  ASSERT_EQ(config.rules.size(), 2u);
  EXPECT_TRUE(config.rules[0].include_body);
  EXPECT_TRUE(config.rules[1].include_body);
}

TEST(RespMonitorConfig, ARuleMayTurnTheBodyOffAgainstTheSection) {
  RespMonitorConfig config;
  std::string error;
  EXPECT_TRUE(ParseRespMonitorConfig(
      R"({"enabled":true,"include_body":true,"rules":[
           {"id":"off","url_regex":"a","include_body":false}]})",
      &config, &error));
  ASSERT_EQ(config.rules.size(), 1u);
  EXPECT_FALSE(config.rules[0].include_body);
}

TEST(RespMonitorConfig, TheBodyLimitIsBounded) {
  const RespMonitorConfig config = ParseOk(R"({
    "enabled": true,
    "rules": [
      {"id": "big", "url_regex": "a", "max_body_bytes": 999999999},
      {"id": "small", "url_regex": "b", "max_body_bytes": 32}
    ]
  })");
  ASSERT_EQ(config.rules.size(), 2u);
  // A limit past the ceiling is clamped to the ceiling: the code does not
  // carry a limit that came from the wire.
  EXPECT_EQ(config.rules[0].max_body_bytes, kRespMonitorMaxBodyBytesCeiling);
  EXPECT_EQ(config.rules[1].max_body_bytes, 32u);
}

TEST(RespMonitorConfig, ANonsenseCountOrLimitSkipsTheRule) {
  const RespMonitorConfig config = ParseOk(R"({
    "enabled": true,
    "rules": [
      {"id": "zero", "url_regex": "a", "max_count": 0},
      {"id": "negative", "url_regex": "b", "max_count": -1},
      {"id": "negbytes", "url_regex": "c", "max_body_bytes": -5}
    ]
  })");
  EXPECT_TRUE(config.rules.empty());
  EXPECT_EQ(config.skipped_rules, 3u);
}

TEST(RespMonitorConfig, TheSectionIsFoundInTheBundle) {
  std::string section;
  std::string error;
  EXPECT_TRUE(FindRespMonitorSection(
      R"({"response_monitor":{"enabled":true,"rules":[]},"other":1})",
      &section, &error));
  EXPECT_NE(section.find("\"enabled\":true"), std::string::npos);
}

TEST(RespMonitorConfig, AMissingSectionIsNotAFailure) {
  std::string section;
  std::string error;
  EXPECT_TRUE(FindRespMonitorSection(R"({"other":1})", &section, &error));
  EXPECT_TRUE(section.empty());
}

TEST(RespMonitorConfig, AMalformedBundleIsAFailure) {
  std::string section;
  std::string error;
  EXPECT_FALSE(FindRespMonitorSection(R"({"response_monitor":)", &section, &error));
  EXPECT_FALSE(error.empty());
}

TEST(RespMonitorConfig, AMalformedSectionIsAFailure) {
  RespMonitorConfig config;
  std::string error;
  EXPECT_FALSE(ParseRespMonitorConfig(R"({"enabled":)", &config, &error));
  EXPECT_FALSE(error.empty());
}

TEST(RespMonitorConfig, TheRegexProbeAgreesWithTheParser) {
  EXPECT_TRUE(IsValidRespMonitorUrlRegex("^https://example\\.test/"));
  EXPECT_FALSE(IsValidRespMonitorUrlRegex("([unclosed"));
  EXPECT_FALSE(IsValidRespMonitorUrlRegex("*bad"));
}

}  // namespace
}  // namespace lengsuo::fp
