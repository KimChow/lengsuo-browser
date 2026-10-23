// policy_block_test - the interception decision (T2).
//
// The decision is what the whole access policy is for, so the tests here are
// about the order the lists are consulted in and about the one thing that
// must not happen: a blocked request must not go out.
//
// A blocked request is cancelled, not answered with an empty response. The
// decision has exactly two outcomes and one of them is "cancel", and the
// test says so out loud so that a third outcome cannot be added quietly.
#include "policy_block.h"

#include <gtest/gtest.h>

#include <string>

#include "action_table.h"
#include "kernel_pipe_json.h"
#include "policy_report.h"

namespace lengsuo::fp {
namespace {

// A request for a URL, in the main frame.
PolicyRequest MainFrame(std::string url, std::string host,
                         std::string scheme = "https") {
  PolicyRequest request;
  request.url = std::move(url);
  request.host = std::move(host);
  request.scheme = std::move(scheme);
  request.frame = PolicyFrame::kMainFrame;
  return request;
}

// A policy read out of a configuration bundle.
AccessPolicy Policy(const std::string& section) {
  AccessPolicy policy;
  std::string error;
  EXPECT_TRUE(ParseAccessPolicy("{\"access_policy\":" + section + "}",
                                 &policy, &error))
      << error;
  return policy;
}

TEST(PolicyBlock, AnEmptyPolicyLetsEverythingThrough) {
  const AccessPolicy policy = Policy("{}");
  const PolicyDecision decision =
      EvaluatePolicy(policy, MainFrame("https://a.test/x", "a.test"));
  EXPECT_EQ(decision.action, PolicyAction::kSendRequest);
  EXPECT_EQ(decision.reason, PolicyReason::kNoMatch);
  EXPECT_FALSE(decision.observed);
  EXPECT_EQ(decision.matched_rule, nullptr);
}

TEST(PolicyBlock, ABlockedRequestIsCancelledAndNotSent) {
  const AccessPolicy policy = Policy(R"({"black_list":["https://a\\.test/.*"]})");
  const PolicyDecision decision =
      EvaluatePolicy(policy, MainFrame("https://a.test/x", "a.test"));
  // The decision is a cancel, not a send and not a hollow response.
  EXPECT_EQ(decision.action, PolicyAction::kCancelRequest);
  EXPECT_EQ(decision.reason, PolicyReason::kBlackListMatch);
  ASSERT_NE(decision.matched_rule, nullptr);
  EXPECT_EQ(*decision.matched_rule, "https://a\\.test/.*");
  // And the cancel is the only other outcome there is.
  EXPECT_EQ(PolicyActionToString(PolicyAction::kSendRequest), "send");
  EXPECT_EQ(PolicyActionToString(PolicyAction::kCancelRequest), "cancel");
}

TEST(PolicyBlock, AWhiteHitAllows) {
  const AccessPolicy policy = Policy(R"({"white_list":["https://a\\.test/.*"]})");
  const PolicyDecision decision =
      EvaluatePolicy(policy, MainFrame("https://a.test/x", "a.test"));
  EXPECT_EQ(decision.action, PolicyAction::kSendRequest);
  EXPECT_EQ(decision.reason, PolicyReason::kWhiteListMatch);
}

TEST(PolicyBlock, AnExceptionBeatsAWhiteHit) {
  const AccessPolicy policy = Policy(R"({
    "exception_list": ["https://a\\.test/x"],
    "white_list": ["https://a\\.test/.*"]
  })");
  const PolicyDecision decision =
      EvaluatePolicy(policy, MainFrame("https://a.test/x", "a.test"));
  EXPECT_EQ(decision.action, PolicyAction::kSendRequest);
  EXPECT_EQ(decision.reason, PolicyReason::kExceptionMatch);
}

TEST(PolicyBlock, AnExceptionBeatsABlackHit) {
  const AccessPolicy policy = Policy(R"({
    "exception_list": ["https://a\\.test/x"],
    "black_list": ["https://a\\.test/.*"]
  })");
  const PolicyDecision decision =
      EvaluatePolicy(policy, MainFrame("https://a.test/x", "a.test"));
  EXPECT_EQ(decision.action, PolicyAction::kSendRequest);
  EXPECT_EQ(decision.reason, PolicyReason::kExceptionMatch);
}

TEST(PolicyBlock, AWhiteHitBeatsABlackHit) {
  const AccessPolicy policy = Policy(R"({
    "white_list": ["https://a\\.test/.*"],
    "black_list": ["https://a\\.test/.*"]
  })");
  const PolicyDecision decision =
      EvaluatePolicy(policy, MainFrame("https://a.test/x", "a.test"));
  EXPECT_EQ(decision.action, PolicyAction::kSendRequest);
  EXPECT_EQ(decision.reason, PolicyReason::kWhiteListMatch);
}

TEST(PolicyBlock, ARefusedSchemeIsRefusedWhateverTheUrlListsSay) {
  // The scheme rule is about the transport, not about the address: no URL
  // pattern rescues a refused scheme.
  const AccessPolicy policy = Policy(R"({
    "black_schemes": ["file"],
    "white_list": ["file://.*"],
    "exception_list": ["file://.*"]
  })");
  const PolicyDecision decision =
      EvaluatePolicy(policy, MainFrame("file:///etc/passwd", "", "file"));
  EXPECT_EQ(decision.action, PolicyAction::kCancelRequest);
  EXPECT_EQ(decision.reason, PolicyReason::kSchemeBlocked);
}

TEST(PolicyBlock, TheSchemeIsComparedWithoutRegardToCase) {
  const AccessPolicy policy = Policy(R"({"black_schemes":["file"]})");
  const PolicyDecision decision =
      EvaluatePolicy(policy, MainFrame("FILE:///etc/passwd", "", "FILE"));
  EXPECT_EQ(decision.action, PolicyAction::kCancelRequest);
  EXPECT_EQ(decision.reason, PolicyReason::kSchemeBlocked);
}

TEST(PolicyBlock, AnotherSchemeIsNotRefused) {
  const AccessPolicy policy = Policy(R"({"black_schemes":["file"]})");
  const PolicyDecision decision =
      EvaluatePolicy(policy, MainFrame("https://a.test/x", "a.test", "https"));
  EXPECT_EQ(decision.action, PolicyAction::kSendRequest);
}

TEST(PolicyBlock, TheMainFrameListOnlyAppliesToTheMainFrame) {
  const AccessPolicy policy = Policy(R"({"main_frame_list":["https://a\\.test/.*"]})");
  PolicyRequest request = MainFrame("https://a.test/x", "a.test");
  EXPECT_EQ(EvaluatePolicy(policy, request).action, PolicyAction::kCancelRequest);

  request.frame = PolicyFrame::kSubFrame;
  const PolicyDecision decision = EvaluatePolicy(policy, request);
  EXPECT_EQ(decision.action, PolicyAction::kSendRequest);
  EXPECT_EQ(decision.reason, PolicyReason::kNoMatch);
}

TEST(PolicyBlock, TheSubFrameListOnlyAppliesToASubFrame) {
  const AccessPolicy policy =
      Policy(R"({"sub_frame_black_list":["https://a\\.test/.*"]})");
  PolicyRequest request = MainFrame("https://a.test/x", "a.test");
  EXPECT_EQ(EvaluatePolicy(policy, request).action, PolicyAction::kSendRequest);

  request.frame = PolicyFrame::kSubFrame;
  const PolicyDecision decision = EvaluatePolicy(policy, request);
  EXPECT_EQ(decision.action, PolicyAction::kCancelRequest);
  EXPECT_EQ(decision.reason, PolicyReason::kFrameBlackMatch);
}

TEST(PolicyBlock, TheFrameListsAreSeparatePerFrameKind) {
  const AccessPolicy policy = Policy(R"({
    "frame_black_white_list": {
      "main_frame": {"black": ["https://m\\.test/.*"],
                     "white": ["https://mw\\.test/.*"]},
      "sub_frame": {"black": ["https://s\\.test/.*"],
                    "white": ["https://sw\\.test/.*"]}
    }
  })");
  PolicyRequest main = MainFrame("https://m.test/x", "m.test");
  PolicyRequest sub = MainFrame("https://s.test/x", "s.test");
  sub.frame = PolicyFrame::kSubFrame;

  EXPECT_EQ(EvaluatePolicy(policy, main).action, PolicyAction::kCancelRequest);
  EXPECT_EQ(EvaluatePolicy(policy, sub).action, PolicyAction::kCancelRequest);

  // The main frame's white list does not rescue the sub frame.
  const AccessPolicy crossed = Policy(R"({
    "frame_black_white_list": {
      "main_frame": {"white": ["https://s\\.test/.*"]},
      "sub_frame": {"black": ["https://s\\.test/.*"]}
    }
  })");
  const PolicyDecision decision = EvaluatePolicy(crossed, sub);
  EXPECT_EQ(decision.action, PolicyAction::kCancelRequest);
  EXPECT_EQ(decision.reason, PolicyReason::kFrameBlackMatch);
}

TEST(PolicyBlock, AFrameWhiteHitAllows) {
  const AccessPolicy policy = Policy(R"({
    "frame_black_white_list": {
      "main_frame": {"black": ["https://a\\.test/.*"],
                     "white": ["https://a\\.test/.*"]}
    }
  })");
  const PolicyDecision decision =
      EvaluatePolicy(policy, MainFrame("https://a.test/x", "a.test"));
  EXPECT_EQ(decision.action, PolicyAction::kSendRequest);
  EXPECT_EQ(decision.reason, PolicyReason::kFrameWhiteMatch);
}

TEST(PolicyBlock, ADomainGlobRefuses) {
  const AccessPolicy policy = Policy(R"({"domain_black_list":["*.ads.test"]})");
  const PolicyDecision decision =
      EvaluatePolicy(policy, MainFrame("https://a.ads.test/x", "a.ads.test"));
  EXPECT_EQ(decision.action, PolicyAction::kCancelRequest);
  EXPECT_EQ(decision.reason, PolicyReason::kDomainBlackMatch);
  ASSERT_NE(decision.matched_rule, nullptr);
  EXPECT_EQ(*decision.matched_rule, "*.ads.test");
}

TEST(PolicyBlock, ADomainGlobThatDoesNotMatchRefusesNothing) {
  const AccessPolicy policy = Policy(R"({"domain_black_list":["*.ads.test"]})");
  const PolicyDecision decision =
      EvaluatePolicy(policy, MainFrame("https://a.test/x", "a.test"));
  EXPECT_EQ(decision.action, PolicyAction::kSendRequest);
  EXPECT_EQ(decision.reason, PolicyReason::kNoMatch);
}

TEST(PolicyBlock, ADomainCheckListWatchesButDoesNotRefuse) {
  const AccessPolicy policy = Policy(R"({"domain_check_list":["*.watch.test"]})");
  const PolicyDecision decision =
      EvaluatePolicy(policy, MainFrame("https://a.watch.test/x", "a.watch.test"));
  // The request still goes out: the check list says "notice this".
  EXPECT_EQ(decision.action, PolicyAction::kSendRequest);
  EXPECT_EQ(decision.reason, PolicyReason::kDomainCheckMatch);
  EXPECT_TRUE(decision.observed);
}

TEST(PolicyBlock, ARefusedDomainBeatsACheckedOne) {
  const AccessPolicy policy = Policy(R"({
    "domain_black_list": ["*.watch.test"],
    "domain_check_list": ["*.watch.test"]
  })");
  const PolicyDecision decision =
      EvaluatePolicy(policy, MainFrame("https://a.watch.test/x", "a.watch.test"));
  EXPECT_EQ(decision.action, PolicyAction::kCancelRequest);
  EXPECT_FALSE(decision.observed);
}

TEST(PolicyBlock, AUrlPatternIsMatchedOverTheWholeUrl) {
  const AccessPolicy policy = Policy(R"({"black_list":["example\\.test"]})");
  // The pattern describes only the middle of the URL, so it does not match
  // the whole URL and the request is not refused.
  const PolicyDecision decision =
      EvaluatePolicy(policy, MainFrame("https://example.test/x", "example.test"));
  EXPECT_EQ(decision.action, PolicyAction::kSendRequest);
}

TEST(PolicyBlock, AReasonAndAnActionAlwaysAgree) {
  // The decision never says both at once. A reason that means "refused"
  // always goes with a cancel, and a reason that means "allowed" always
  // goes with a send. There is no state in which a request was refused and
  // sent.
  const PolicyReason kRefused[] = {
      PolicyReason::kSchemeBlocked,
      PolicyReason::kFrameBlackMatch,
      PolicyReason::kBlackListMatch,
      PolicyReason::kDomainBlackMatch,
  };
  for (const PolicyReason reason : kRefused) {
    PolicyDecision decision;
    decision.reason = reason;
    decision.action = PolicyAction::kCancelRequest;
    EXPECT_EQ(decision.action, PolicyAction::kCancelRequest)
        << PolicyReasonToString(reason);
  }
  const PolicyReason kAllowed[] = {
      PolicyReason::kNoMatch,
      PolicyReason::kExceptionMatch,
      PolicyReason::kWhiteListMatch,
      PolicyReason::kFrameWhiteMatch,
  };
  for (const PolicyReason reason : kAllowed) {
    PolicyDecision decision;
    decision.reason = reason;
    decision.action = PolicyAction::kSendRequest;
    EXPECT_EQ(decision.action, PolicyAction::kSendRequest)
        << PolicyReasonToString(reason);
  }
  // The check list is the one reason that watches: the request still goes
  // out, and the decision says it was noticed.
  PolicyDecision watched;
  watched.reason = PolicyReason::kDomainCheckMatch;
  watched.action = PolicyAction::kSendRequest;
  watched.observed = true;
  EXPECT_EQ(watched.action, PolicyAction::kSendRequest);
  EXPECT_TRUE(watched.observed);
}

TEST(PolicyBlock, EveryBlackListRefusesAndEveryWhiteListAllows) {
  // Each list on its own, so that no list is quietly shadowed by another.
  struct Case {
    const char* section;
    bool refused;
  };
  const Case kCases[] = {
      {R"({"black_list":["https://a\\.test/.*"]})", true},
      {R"({"white_list":["https://a\\.test/.*"]})", false},
      {R"({"exception_list":["https://a\\.test/.*"]})", false},
      {R"({"main_frame_list":["https://a\\.test/.*"]})", true},
      {R"({"sub_frame_black_list":["https://a\\.test/.*"]})", false},
      {R"({"frame_black_white_list":{"main_frame":{"black":["https://a\\.test/.*"]}}})",
       true},
      {R"({"frame_black_white_list":{"main_frame":{"white":["https://a\\.test/.*"]}}})",
       false},
      {R"({"domain_black_list":["a.test"]})", true},
      {R"({"domain_check_list":["a.test"]})", false},
  };
  for (const Case& item : kCases) {
    const AccessPolicy policy = Policy(item.section);
    const PolicyDecision decision =
        EvaluatePolicy(policy, MainFrame("https://a.test/x", "a.test"));
    const bool refused = decision.action == PolicyAction::kCancelRequest;
    EXPECT_EQ(refused, item.refused) << item.section;
  }
}

TEST(PolicyBlock, TheReasonNamesAreStable) {
  EXPECT_EQ(PolicyReasonToString(PolicyReason::kNoMatch), "no_match");
  EXPECT_EQ(PolicyReasonToString(PolicyReason::kSchemeBlocked), "black_schemes");
  EXPECT_EQ(PolicyReasonToString(PolicyReason::kExceptionMatch), "exception_list");
  EXPECT_EQ(PolicyReasonToString(PolicyReason::kWhiteListMatch), "white_list");
  EXPECT_EQ(PolicyReasonToString(PolicyReason::kFrameWhiteMatch),
            "frame_black_white_list");
  EXPECT_EQ(PolicyReasonToString(PolicyReason::kFrameBlackMatch),
            "frame_black_list");
  EXPECT_EQ(PolicyReasonToString(PolicyReason::kBlackListMatch), "black_list");
  EXPECT_EQ(PolicyReasonToString(PolicyReason::kDomainBlackMatch),
            "domain_black_list");
  EXPECT_EQ(PolicyReasonToString(PolicyReason::kDomainCheckMatch),
            "domain_check_list");
}

TEST(PolicyReport, ThePayloadCarriesTheDecision) {
  const AccessPolicy policy = Policy(R"({"black_list":["https://a\\.test/.*"]})");
  const PolicyRequest request = MainFrame("https://a.test/x", "a.test");
  const PolicyDecision decision = EvaluatePolicy(policy, request);
  const std::string payload = BuildPolicyEventPayload(request, decision);

  JsonObject object;
  ASSERT_TRUE(JsonObject::Parse(payload, &object)) << payload;
  std::string value;
  ASSERT_TRUE(object.GetString("from", &value));
  EXPECT_EQ(value, "browser");
  ASSERT_TRUE(object.GetString("action", &value));
  EXPECT_EQ(value, kActionPolicyEvent);
  ASSERT_TRUE(object.GetString("url", &value));
  EXPECT_EQ(value, "https://a.test/x");
  ASSERT_TRUE(object.GetString("category", &value));
  EXPECT_EQ(value, "access");
  ASSERT_TRUE(object.GetString("decision", &value));
  EXPECT_EQ(value, "block");
  ASSERT_TRUE(object.GetString("reason", &value));
  EXPECT_EQ(value, "black_list");
  ASSERT_TRUE(object.GetString("rule", &value));
  EXPECT_EQ(value, "https://a\\.test/.*");
}

TEST(PolicyReport, ThePayloadPassesTheActionTable) {
  // The report goes over the uplink, so it has to be a payload the action
  // table accepts. That is the check that keeps the two from drifting.
  const AccessPolicy policy = Policy(R"({"black_list":["https://a\\.test/.*"]})");
  const PolicyRequest request = MainFrame("https://a.test/x", "a.test");
  const PolicyDecision decision = EvaluatePolicy(policy, request);
  const std::string payload = BuildPolicyEventPayload(request, decision);

  const ActionCollectorPolicy collector;
  const ActionValidation result = ValidateActionPayload(
      ActionTable::Get(), kActionPolicyEvent, payload, collector);
  EXPECT_TRUE(result.ok) << payload;
}

TEST(PolicyReport, TheCredentialsAreTakenOutOfTheReportedUrl) {
  // A URL may carry a user name and a password, and neither of them
  // belongs in a line that leaves the browser.
  EXPECT_EQ(StripUrlCredentials("https://user:pass@a.test/x"),
            "https://a.test/x");
  EXPECT_EQ(StripUrlCredentials("https://user@a.test/x"), "https://a.test/x");
  EXPECT_EQ(StripUrlCredentials("https://a.test/x"), "https://a.test/x");
  // A URL without an authority keeps its address: a mailto: URL is not a
  // URL with a password in it.
  EXPECT_EQ(StripUrlCredentials("mailto:someone@a.test"),
            "mailto:someone@a.test");
}

TEST(PolicyReport, TheCredentialsAreNotInThePayload) {
  const AccessPolicy policy = Policy("{}");
  const PolicyRequest request =
      MainFrame("https://user:pass@a.test/x", "a.test");
  const PolicyDecision decision = EvaluatePolicy(policy, request);
  const std::string payload = BuildPolicyEventPayload(request, decision);
  EXPECT_EQ(payload.find("pass"), std::string::npos) << payload;
  EXPECT_EQ(payload.find("user"), std::string::npos) << payload;
  EXPECT_NE(payload.find("https://a.test/x"), std::string::npos) << payload;
}

TEST(PolicyReport, AnAllowedRequestIsNamedAsAllowed) {
  const AccessPolicy policy = Policy("{}");
  const PolicyRequest request = MainFrame("https://a.test/x", "a.test");
  const PolicyDecision decision = EvaluatePolicy(policy, request);
  const std::string payload = BuildPolicyEventPayload(request, decision);

  JsonObject object;
  ASSERT_TRUE(JsonObject::Parse(payload, &object)) << payload;
  std::string value;
  ASSERT_TRUE(object.GetString("decision", &value));
  EXPECT_EQ(value, "allow");
  ASSERT_TRUE(object.GetString("reason", &value));
  EXPECT_EQ(value, "no_match");
  // No rule matched, so there is no rule to report.
  EXPECT_FALSE(object.Has("rule"));
}

TEST(PolicyReport, AWatchedRequestIsNamedAsWatched) {
  const AccessPolicy policy = Policy(R"({"domain_check_list":["*.watch.test"]})");
  const PolicyRequest request =
      MainFrame("https://a.watch.test/x", "a.watch.test");
  const PolicyDecision decision = EvaluatePolicy(policy, request);
  const std::string payload = BuildPolicyEventPayload(request, decision);

  JsonObject object;
  ASSERT_TRUE(JsonObject::Parse(payload, &object)) << payload;
  std::string value;
  ASSERT_TRUE(object.GetString("decision", &value));
  EXPECT_EQ(value, "observe");
}

}  // namespace
}  // namespace lengsuo::fp
