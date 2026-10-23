// policy_rules_test - reading the access policy out of the configuration.
//
// The rules are configuration, so the tests are about what happens to a
// configuration that is not quite right. The two rules that are not
// negotiable get their own tests:
//
//   * a rule that cannot be read is dropped and counted, and the rest of the
//     policy still applies;
//   * a configuration that has not arrived is an empty policy, and an empty
//     policy refuses nothing.
#include "policy_rules.h"

#include <gtest/gtest.h>

#include <string>

namespace lengsuo::fp {
namespace {

// A configuration bundle with the given access_policy section.
std::string Bundle(const std::string& section) {
  return "{\"access_policy\":" + section + "}";
}

// Reads the policy out of a bundle, failing the test if it cannot be read.
AccessPolicy Read(const std::string& bundle) {
  AccessPolicy policy;
  std::string error;
  EXPECT_TRUE(ParseAccessPolicy(bundle, &policy, &error)) << error;
  return policy;
}

TEST(PolicyRules, ReadsTheUrlLists) {
  const AccessPolicy policy = Read(Bundle(R"({
    "black_list": ["https://a.test/.*"],
    "white_list": ["https://b.test/.*"],
    "exception_list": ["https://c.test/.*"]
  })"));
  ASSERT_EQ(policy.black_list.size(), 1u);
  EXPECT_EQ(policy.black_list[0], "https://a.test/.*");
  ASSERT_EQ(policy.white_list.size(), 1u);
  EXPECT_EQ(policy.white_list[0], "https://b.test/.*");
  ASSERT_EQ(policy.exception_list.size(), 1u);
  EXPECT_EQ(policy.exception_list[0], "https://c.test/.*");
  EXPECT_EQ(policy.skipped_rules, 0u);
}

TEST(PolicyRules, ReadsTheFrameLists) {
  const AccessPolicy policy = Read(Bundle(R"({
    "main_frame_list": ["https://m.test/.*"],
    "sub_frame_black_list": ["https://s.test/.*"],
    "frame_black_white_list": {
      "main_frame": {"black": ["https://mb.test/.*"],
                     "white": ["https://mw.test/.*"]},
      "sub_frame": {"black": ["https://sb.test/.*"],
                    "white": ["https://sw.test/.*"]}
    }
  })"));
  ASSERT_EQ(policy.main_frame_list.size(), 1u);
  EXPECT_EQ(policy.main_frame_list[0], "https://m.test/.*");
  ASSERT_EQ(policy.sub_frame_black_list.size(), 1u);
  EXPECT_EQ(policy.sub_frame_black_list[0], "https://s.test/.*");
  ASSERT_EQ(policy.main_frame_rules.black.size(), 1u);
  EXPECT_EQ(policy.main_frame_rules.black[0], "https://mb.test/.*");
  ASSERT_EQ(policy.main_frame_rules.white.size(), 1u);
  EXPECT_EQ(policy.main_frame_rules.white[0], "https://mw.test/.*");
  ASSERT_EQ(policy.sub_frame_rules.black.size(), 1u);
  EXPECT_EQ(policy.sub_frame_rules.black[0], "https://sb.test/.*");
  ASSERT_EQ(policy.sub_frame_rules.white.size(), 1u);
  EXPECT_EQ(policy.sub_frame_rules.white[0], "https://sw.test/.*");
}

TEST(PolicyRules, ReadsTheDomainLists) {
  const AccessPolicy policy = Read(Bundle(R"({
    "domain_black_list": ["*.ads.test"],
    "domain_check_list": ["*.watch.test"]
  })"));
  ASSERT_EQ(policy.domain_black_list.size(), 1u);
  EXPECT_EQ(policy.domain_black_list[0], "*.ads.test");
  ASSERT_EQ(policy.domain_check_list.size(), 1u);
  EXPECT_EQ(policy.domain_check_list[0], "*.watch.test");
}

TEST(PolicyRules, TheSchemeListIsCanonicalised) {
  // A scheme is written with or without its colon, in either case, and it
  // is compared without regard to either.
  const AccessPolicy policy = Read(Bundle(R"({
    "black_schemes": ["file:", "FTP", "ws:"]
  })"));
  ASSERT_EQ(policy.black_schemes.size(), 3u);
  EXPECT_EQ(policy.black_schemes[0], "file");
  EXPECT_EQ(policy.black_schemes[1], "ftp");
  EXPECT_EQ(policy.black_schemes[2], "ws");
}

TEST(PolicyRules, AConfigurationThatHasNotArrivedRefusesNothing) {
  // No section at all: the platform did not ask for a policy, so there is
  // nothing to enforce. A browser that refuses everything because the
  // runner is not up yet is a broken browser.
  AccessPolicy policy;
  std::string error;
  ASSERT_TRUE(ParseAccessPolicy("{\"other\":1}", &policy, &error)) << error;
  EXPECT_TRUE(policy.black_list.empty());
  EXPECT_TRUE(policy.white_list.empty());
  EXPECT_TRUE(policy.black_schemes.empty());
  EXPECT_EQ(policy.skipped_rules, 0u);
}

TEST(PolicyRules, AnUnreadableBundleLeavesAnEmptyPolicy) {
  AccessPolicy policy;
  std::string error;
  EXPECT_FALSE(ParseAccessPolicy("{not json", &policy, &error));
  EXPECT_FALSE(error.empty());
  // The policy is empty, not half-read: a failed read leaves nothing
  // behind that could refuse a request.
  EXPECT_TRUE(policy.black_list.empty());
  EXPECT_TRUE(policy.white_list.empty());
}

TEST(PolicyRules, ASectionThatIsNotAnObjectIsAnUnreadableBundle) {
  AccessPolicy policy;
  std::string error;
  EXPECT_FALSE(ParseAccessPolicy("{\"access_policy\":[1,2]}", &policy, &error));
  EXPECT_TRUE(policy.black_list.empty());
}

TEST(PolicyRules, AListThatIsNotAnArrayIsDroppedAndCounted) {
  // The rest of the policy still applies: one bad list does not take the
  // whole policy with it.
  const AccessPolicy policy = Read(Bundle(R"({
    "black_list": "not a list",
    "white_list": ["https://b.test/.*"]
  })"));
  EXPECT_TRUE(policy.black_list.empty());
  ASSERT_EQ(policy.white_list.size(), 1u);
  EXPECT_EQ(policy.skipped_rules, 1u);
}

TEST(PolicyRules, AListThatIsNotMadeOfStringsIsDroppedAndCounted) {
  const AccessPolicy policy = Read(Bundle(R"({
    "black_list": [1, 2]
  })"));
  EXPECT_TRUE(policy.black_list.empty());
  EXPECT_EQ(policy.skipped_rules, 1u);
}

TEST(PolicyRules, AUrlPatternTheMatcherCannotReadIsDroppedAndCounted) {
  // The rule that cannot fire must not fail the request that trips over
  // it, and it must not take the good rules with it.
  const AccessPolicy policy = Read(Bundle(R"({
    "black_list": ["https://good.test/.*", "(unclosed", "https://also-good.test/.*"]
  })"));
  ASSERT_EQ(policy.black_list.size(), 2u);
  EXPECT_EQ(policy.black_list[0], "https://good.test/.*");
  EXPECT_EQ(policy.black_list[1], "https://also-good.test/.*");
  EXPECT_EQ(policy.skipped_rules, 1u);
}

TEST(PolicyRules, ADomainPatternIsNeverDroppedForBeingUnreadable) {
  // Every byte sequence is a valid glob, so the domain lists are only ever
  // dropped as a whole.
  const AccessPolicy policy = Read(Bundle(R"({
    "domain_black_list": ["*.a.test", "??b", "[weird]"]
  })"));
  EXPECT_EQ(policy.domain_black_list.size(), 3u);
  EXPECT_EQ(policy.skipped_rules, 0u);
}

TEST(PolicyRules, AnEmptySchemeIsDroppedAndCounted) {
  const AccessPolicy policy = Read(Bundle(R"({"black_schemes":["", ":"]})"));
  EXPECT_TRUE(policy.black_schemes.empty());
  EXPECT_EQ(policy.skipped_rules, 2u);
}

TEST(PolicyRules, AFrameSectionThatIsNotAnObjectIsDroppedAndCounted) {
  const AccessPolicy policy = Read(Bundle(R"({
    "frame_black_white_list": "not an object",
    "black_list": ["https://a.test/.*"]
  })"));
  EXPECT_TRUE(policy.main_frame_rules.black.empty());
  ASSERT_EQ(policy.black_list.size(), 1u);
  EXPECT_EQ(policy.skipped_rules, 1u);
}

TEST(PolicyRules, AFrameListThatIsNotAnArrayIsDroppedAndCounted) {
  const AccessPolicy policy = Read(Bundle(R"({
    "frame_black_white_list": {
      "main_frame": {"black": "not a list", "white": ["https://w.test/.*"]},
      "sub_frame": {"black": ["https://b.test/.*"]}
    }
  })"));
  EXPECT_TRUE(policy.main_frame_rules.black.empty());
  ASSERT_EQ(policy.main_frame_rules.white.size(), 1u);
  EXPECT_EQ(policy.main_frame_rules.white[0], "https://w.test/.*");
  ASSERT_EQ(policy.sub_frame_rules.black.size(), 1u);
  EXPECT_EQ(policy.skipped_rules, 1u);
}

TEST(PolicyRules, AnEmptyPolicyIsEmpty) {
  const AccessPolicy policy = Read(Bundle("{}"));
  EXPECT_TRUE(policy.black_list.empty());
  EXPECT_TRUE(policy.white_list.empty());
  EXPECT_TRUE(policy.exception_list.empty());
  EXPECT_TRUE(policy.black_schemes.empty());
  EXPECT_TRUE(policy.main_frame_list.empty());
  EXPECT_TRUE(policy.sub_frame_black_list.empty());
  EXPECT_TRUE(policy.domain_black_list.empty());
  EXPECT_TRUE(policy.domain_check_list.empty());
  EXPECT_EQ(policy.skipped_rules, 0u);
}

TEST(PolicyRules, AnEscapedPatternIsReadAsItWasWritten) {
  const AccessPolicy policy = Read(Bundle(R"({
    "black_list": ["https://a\\.test/.*"]
  })"));
  ASSERT_EQ(policy.black_list.size(), 1u);
  EXPECT_EQ(policy.black_list[0], "https://a\\.test/.*");
}

}  // namespace
}  // namespace lengsuo::fp
