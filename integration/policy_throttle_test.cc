// policy_throttle_test - what the request path does with a decision
// (task 0018).
//
// The class that talks to Chromium (PolicyThrottle) cannot be compiled here:
// a plain g++ has no blink::URLLoaderThrottle. What is compiled here is the
// decision the throttle carries out, which is the part that can be wrong
// silently - and the three cases the card names are exactly the three that
// must not drift:
//
//   * a block hit cancels the request;
//   * an allow hit does not;
//   * a request no rule matched is neither cancelled nor reported.
//
// The last one is not an optimisation. "No rule matched" is the absence of a
// decision, and reporting it would put one line on the uplink for every
// request the browser makes - a report channel that says nothing, thousands
// of times a minute. The test says so out loud so it cannot be relaxed
// quietly.
//
// What the throttle does with the outcome - mount nothing when there are no
// rules, call CancelWithError, enqueue the payload - is one call per branch
// and is covered by the real build and the acceptance run, which is where a
// Chromium type can be linked at all.
#include "lengsuo/integration/policy_throttle.h"

#include <gtest/gtest.h>

#include <string>
#include <string_view>

#include "lengsuo/native/action_table.h"
#include "lengsuo/native/policy_block.h"
#include "lengsuo/native/policy_match.h"
#include "lengsuo/native/policy_report.h"
#include "lengsuo/native/policy_rules.h"

namespace lengsuo {
namespace {

// A ConfigBundle whose `access_policy` section is `section`.
std::string Bundle(const std::string& section) {
  return "{\"access_policy\":" + section + "}";
}

// The policy read out of that section.
fp::AccessPolicy PolicyFrom(const std::string& section) {
  fp::AccessPolicy policy;
  EXPECT_TRUE(ReadAccessPolicy(Bundle(section), &policy));
  return policy;
}

// A main-frame https request as the browser hands it to the throttle.
fp::PolicyRequest Request(std::string url, std::string host) {
  return BuildPolicyRequest(std::move(url), std::move(host), "https",
                            fp::PolicyFrame::kMainFrame);
}

TEST(PolicyThrottleTest, NoRuleMatchedIsNeitherCancelledNorReported) {
  // A policy with rules, none of which match this request. The policy still
  // has an opinion - it is simply "nothing here applies".
  const fp::AccessPolicy policy =
      PolicyFrom(R"({"black_list":["https://blocked\\.test/.*"]})");
  const PolicyOutcome outcome =
      DecidePolicy(policy, Request("https://a.test/x", "a.test"));

  EXPECT_FALSE(outcome.cancel);
  EXPECT_FALSE(outcome.report);
  // Nothing was built for a report that is not sent.
  EXPECT_TRUE(outcome.payload.empty());
  EXPECT_EQ(outcome.decision.reason, fp::PolicyReason::kNoMatch);
}

TEST(PolicyThrottleTest, ABlockListHitCancelsAndReports) {
  const fp::AccessPolicy policy =
      PolicyFrom(R"({"black_list":["https://blocked\\.test/.*"]})");
  const PolicyOutcome outcome =
      DecidePolicy(policy, Request("https://blocked.test/x", "blocked.test"));

  // The request does not go out.
  EXPECT_TRUE(outcome.cancel);
  EXPECT_EQ(outcome.decision.action, fp::PolicyAction::kCancelRequest);
  EXPECT_EQ(outcome.decision.reason, fp::PolicyReason::kBlackListMatch);
  ASSERT_NE(outcome.decision.matched_rule, nullptr);
  EXPECT_EQ(*outcome.decision.matched_rule, "https://blocked\\.test/.*");
  // And the decision is reported.
  EXPECT_TRUE(outcome.report);
  EXPECT_NE(outcome.payload.find("\"decision\":\"block\""), std::string::npos);
  EXPECT_NE(outcome.payload.find("\"reason\":\"black_list\""),
            std::string::npos);
}

TEST(PolicyThrottleTest, AWhiteListHitIsReportedButNotCancelled) {
  const fp::AccessPolicy policy =
      PolicyFrom(R"({"white_list":["https://allowed\\.test/.*"]})");
  const PolicyOutcome outcome =
      DecidePolicy(policy, Request("https://allowed.test/x", "allowed.test"));

  // An allow hit is a decision and is reported, but it does not cancel.
  EXPECT_FALSE(outcome.cancel);
  EXPECT_EQ(outcome.decision.action, fp::PolicyAction::kSendRequest);
  EXPECT_EQ(outcome.decision.reason, fp::PolicyReason::kWhiteListMatch);
  EXPECT_TRUE(outcome.report);
  EXPECT_NE(outcome.payload.find("\"decision\":\"allow\""),
            std::string::npos);
}

TEST(PolicyThrottleTest, ARefusedSchemeCancelsBeforeTheRequestGoesOut) {
  // The scheme list is independent of the URL lists and needs no URL match.
  const fp::AccessPolicy policy = PolicyFrom(R"({"black_schemes":["ftp"]})");
  fp::PolicyRequest request = Request("ftp://files.test/x", "files.test");
  request.scheme = "ftp";
  const PolicyOutcome outcome = DecidePolicy(policy, request);

  EXPECT_TRUE(outcome.cancel);
  EXPECT_EQ(outcome.decision.reason, fp::PolicyReason::kSchemeBlocked);
  EXPECT_TRUE(outcome.report);
  EXPECT_NE(outcome.payload.find("\"decision\":\"block\""),
            std::string::npos);
}

TEST(PolicyThrottleTest, ACheckListHitIsObservedAndStillSent) {
  // domain_check_list watches: the request goes out, and the report says it
  // was noticed rather than refused.
  const fp::AccessPolicy policy =
      PolicyFrom(R"({"domain_check_list":["*.watched.test"]})");
  const PolicyOutcome outcome =
      DecidePolicy(policy, Request("https://a.watched.test/x", "a.watched.test"));

  EXPECT_FALSE(outcome.cancel);
  EXPECT_TRUE(outcome.decision.observed);
  EXPECT_EQ(outcome.decision.reason, fp::PolicyReason::kDomainCheckMatch);
  EXPECT_TRUE(outcome.report);
  EXPECT_NE(outcome.payload.find("\"decision\":\"observe\""),
            std::string::npos);
}

TEST(PolicyThrottleTest, ABlockedDecisionPayloadPassesTheActionTable) {
  // The payload the throttle enqueues has to satisfy the action the pipe
  // files it under, or EnqueueEvent rejects it and the decision is lost.
  const fp::AccessPolicy policy =
      PolicyFrom(R"({"black_list":["https://blocked\\.test/.*"]})");
  const PolicyOutcome outcome =
      DecidePolicy(policy, Request("https://blocked.test/x", "blocked.test"));
  ASSERT_TRUE(outcome.report);

  const fp::ActionCollectorPolicy collector;
  std::string action;
  const fp::ActionValidation validation = fp::ValidateActionPayload(
      fp::ActionTable::Get(), fp::kActionPolicyEvent, outcome.payload,
      collector, &action);
  EXPECT_TRUE(validation.ok);
  EXPECT_EQ(action, fp::kActionPolicyEvent);
}

TEST(PolicyThrottleTest, AnEmptyPolicyHasNoRulesToMount) {
  // No section at all: the platform did not ask for a policy. The read
  // succeeds - it is an empty policy, not an error - and it is not worth
  // mounting.
  fp::AccessPolicy policy;
  EXPECT_TRUE(ReadAccessPolicy("{}", &policy));
  EXPECT_FALSE(PolicyHasRules(policy));

  // A section that is present but carries nothing is the same thing.
  EXPECT_FALSE(PolicyHasRules(PolicyFrom("{}")));
}

TEST(PolicyThrottleTest, APolicyWithOneRuleIsWorthMounting) {
  EXPECT_TRUE(PolicyHasRules(
      PolicyFrom(R"({"white_list":["https://allowed\\.test/.*"]})")));
  // Every list counts, including the ones that only watch.
  EXPECT_TRUE(PolicyHasRules(PolicyFrom(R"({"domain_check_list":["*.a.test"]})")));
  EXPECT_TRUE(PolicyHasRules(PolicyFrom(R"({"black_schemes":["file"]})")));
  EXPECT_TRUE(PolicyHasRules(PolicyFrom(R"({"black_schemes":["file"]})")));
}

TEST(PolicyThrottleTest, AMalformedBundleReadsAsAnEmptyPolicy) {
  // The read fails and the output is left empty, so a caller that ignores
  // the return value still mounts nothing rather than blocking everything.
  fp::AccessPolicy policy;
  EXPECT_FALSE(ReadAccessPolicy("{not json", &policy));
  EXPECT_FALSE(PolicyHasRules(policy));
}

TEST(PolicyThrottleTest, BuildPolicyRequestCarriesTheBrowsersOwnFields) {
  // The URL, its host and its scheme are handed in rather than parsed out
  // again: the browser already has them and they must not be re-derived.
  const fp::PolicyRequest request =
      BuildPolicyRequest("https://a.test/x?y=1", "a.test", "https",
                         fp::PolicyFrame::kSubFrame);
  EXPECT_EQ(request.url, "https://a.test/x?y=1");
  EXPECT_EQ(request.host, "a.test");
  EXPECT_EQ(request.scheme, "https");
  EXPECT_EQ(request.frame, fp::PolicyFrame::kSubFrame);
}

}  // namespace
}  // namespace lengsuo