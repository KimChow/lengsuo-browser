// resp_throttle_test - what the response path does with a decision (task 0017).
//
// The class that talks to Chromium (RespThrottle) cannot be compiled here: a
// plain g++ has no blink::URLLoaderThrottle, no mojo, no data pipes. What is
// compiled here is everything the throttle decides and everything it hands
// on, which is the part that can be wrong silently: which responses the rules
// claim, when a rule has said all it had to say about one URL, and what
// leaves the browser on the uplink.
//
// The page's bytes are the one thing this feature may not touch. That is
// judged in native/resp_tee_test.cc, byte for byte; the first test here
// re-proves it across the whole decision path - decide, then fan - so the two
// halves cannot drift apart.
//
// NOT covered here, and cannot be: that the data-pipe plumbing in
// resp_throttle.cc moves these bytes and no others. That is a claim about two
// mojo pipes and a watcher, and it is the dispatcher's end-to-end comparison
// after the merge.
#include "lengsuo/integration/resp_throttle.h"

#include <gtest/gtest.h>

#include <cstddef>
#include <string>
#include <string_view>

#include "lengsuo/native/action_table.h"
#include "lengsuo/native/resp_monitor.h"
#include "lengsuo/native/resp_monitor_config.h"
#include "lengsuo/native/resp_monitor_report.h"
#include "lengsuo/native/resp_tee.h"

namespace lengsuo {
namespace {

// A ConfigBundle whose `response_monitor` section is `section`.
std::string Bundle(const std::string& section) {
  return "{\"response_monitor\":" + section + "}";
}

// The configuration read out of that section.
fp::RespMonitorConfig ConfigFrom(const std::string& section) {
  fp::RespMonitorConfig config;
  EXPECT_TRUE(ReadRespMonitorConfig(Bundle(section), &config));
  return config;
}

// A configuration with its rules installed, which is what the throttle has by
// the time it sees a response.
fp::RespMonitorState Installed(const std::string& section) {
  fp::RespMonitorState state;
  fp::RespMonitorSetRules(&state, ConfigFrom(section));
  return state;
}

// A page's https request, as the throttle builds it from the request it
// captured in WillStartRequest.
fp::RespMonitorRequest Request(std::string url) {
  return BuildRespMonitorRequest(std::move(url), "https", "https");
}

// One rule claiming every JSON response under a host, keeping 64 bytes.
constexpr char kJsonSection[] = R"({"enabled":true,"rules":[
  {"id":"api-json","url_regex":"^https://api\\.test/",
   "content_type":"application/json","max_count":1,"max_body_bytes":64}]})";

// A body longer than that rule's 64-byte limit, so truncation happens.
std::string LongBody() {
  std::string body;
  for (int i = 0; i < 40; ++i) {
    body += "0123456789";
  }
  return body;
}

TEST(RespThrottleTest, AClaimedResponseIsMonitoredAndThePageGetsEveryByte) {
  fp::RespMonitorState state = Installed(kJsonSection);
  const RespMonitorOutcome outcome = DecideRespMonitor(
      &state, Request("https://api.test/v1/items"), "application/json",
      std::string());

  ASSERT_TRUE(outcome.monitor);
  EXPECT_EQ(outcome.rule_id, "api-json");
  EXPECT_EQ(outcome.capture_limit, 64u);
  EXPECT_FALSE(outcome.include_body);

  // The whole path, once: decide, then fan the body out.
  const std::string body = LongBody();
  fp::RespTee tee;
  fp::RespTeeReset(&tee, outcome.capture_limit);
  std::string page;
  fp::RespTeeFan(&tee, body, &page);

  // The page's copy is the body, byte for byte. This is the regression the
  // card names: a tee that forwarded only the captured prefix would still
  // produce a correct-looking report and a broken page.
  EXPECT_EQ(page, body);
  EXPECT_EQ(RespTeePageDigest(tee), fp::RespMonitorBodyDigest(body));
  // The capture kept the rule's limit and knows it was cut short.
  EXPECT_EQ(tee.capture.bytes.size(), 64u);
  EXPECT_EQ(tee.capture.bytes, body.substr(0, 64));
  EXPECT_TRUE(tee.capture.truncated);
  // And the report counts the whole body, not the part that was kept.
  const fp::RespMonitorReport report =
      BuildRespMonitorReport(outcome, "https://api.test/v1/items", 200,
                             "application/json", tee);
  EXPECT_EQ(report.body_bytes, static_cast<std::uint64_t>(body.size()));
  EXPECT_TRUE(report.truncated);
  EXPECT_TRUE(report.body.empty());
}

TEST(RespThrottleTest, ThePerUrlCeilingHoldsAcrossARemount) {
  // The ceiling is per rule and per URL: a rule that says one allows one hit
  // for one URL and still watches every other URL. Remounting the same rules
  // - the configuration is pushed down once per storage partition - must not
  // renew it, or a rule that said "once" would say it once per partition.
  const fp::RespMonitorConfig config = ConfigFrom(kJsonSection);
  fp::RespMonitorState state;
  fp::RespMonitorSetRules(&state, config);

  EXPECT_TRUE(DecideRespMonitor(&state, Request("https://api.test/a"),
                                "application/json", std::string())
                  .monitor);
  EXPECT_FALSE(DecideRespMonitor(&state, Request("https://api.test/a"),
                                 "application/json", std::string())
                   .monitor);
  EXPECT_TRUE(DecideRespMonitor(&state, Request("https://api.test/b"),
                                "application/json", std::string())
                  .monitor);

  fp::RespMonitorSetRules(&state, config);
  EXPECT_FALSE(DecideRespMonitor(&state, Request("https://api.test/a"),
                                 "application/json", std::string())
                   .monitor);
}

TEST(RespThrottleTest, AResponseNoRuleClaimsIsNotMonitored) {
  fp::RespMonitorState state = Installed(kJsonSection);

  // Right media type, wrong URL.
  EXPECT_FALSE(DecideRespMonitor(&state, Request("https://other.test/v1"),
                                 "application/json", std::string())
                   .monitor);
  // Right URL, wrong media type.
  EXPECT_FALSE(DecideRespMonitor(&state, Request("https://api.test/v1"),
                                 "text/html", std::string())
                   .monitor);
}

TEST(RespThrottleTest, TheBrowsersOwnRequestIsOutOfReach) {
  // No initiator origin is the browser's own request, and an extension's is
  // not a page. Neither is a response the monitor reads, however well the URL
  // matches the rule.
  fp::RespMonitorState state = Installed(kJsonSection);
  fp::RespMonitorRequest browser_own =
      BuildRespMonitorRequest("https://api.test/v1", "https", std::string());
  EXPECT_FALSE(DecideRespMonitor(&state, browser_own, "application/json",
                                 std::string())
                   .monitor);

  fp::RespMonitorRequest from_extension = BuildRespMonitorRequest(
      "https://api.test/v1", "https", "chrome-extension");
  EXPECT_FALSE(DecideRespMonitor(&state, from_extension, "application/json",
                                 std::string())
                   .monitor);
}

TEST(RespThrottleTest, ACompressedBodyIsLeftAloneAndSpendsNoHit) {
  // The guard itself: an absent header and `identity` are "decoded", a list
  // is read as a list, and anything else means the body is not readable.
  EXPECT_TRUE(RespMonitorMayReadBody(std::string()));
  EXPECT_TRUE(RespMonitorMayReadBody("identity"));
  EXPECT_TRUE(RespMonitorMayReadBody("  identity  "));
  EXPECT_TRUE(RespMonitorMayReadBody("identity, identity"));
  EXPECT_FALSE(RespMonitorMayReadBody("gzip"));
  EXPECT_FALSE(RespMonitorMayReadBody("IDENTITY, gzip"));
  EXPECT_FALSE(RespMonitorMayReadBody("deflate"));

  // And the guard is applied before the match, so a compressed body is not
  // read and does not spend the rule's allowance on that URL.
  fp::RespMonitorState state = Installed(kJsonSection);
  const fp::RespMonitorRequest request = Request("https://api.test/v1");
  EXPECT_FALSE(DecideRespMonitor(&state, request, "application/json", "gzip")
                   .monitor);
  EXPECT_FALSE(
      DecideRespMonitor(&state, request, "application/json", "br").monitor);
  EXPECT_TRUE(DecideRespMonitor(&state, request, "application/json",
                                "identity")
                  .monitor);
  EXPECT_FALSE(DecideRespMonitor(&state, request, "application/json",
                                 std::string())
                   .monitor);
}

TEST(RespThrottleTest, ARuleWithAnUncompilableRegexIsDroppedAndNotFatal) {
  // One typo in one rule must not take the others with it.
  const fp::RespMonitorConfig config = ConfigFrom(R"({"enabled":true,"rules":[
    {"id":"typo","url_regex":"("},
    {"id":"good","url_regex":"^https://api\\.test/","max_count":2}]})");
  ASSERT_TRUE(config.enabled);
  ASSERT_EQ(config.rules.size(), 1u);
  EXPECT_EQ(config.skipped_rules, 1u);
  EXPECT_EQ(config.rules[0].id, "good");

  fp::RespMonitorState state;
  fp::RespMonitorSetRules(&state, config);
  // The dropped rule cannot claim anything; the surviving one can.
  EXPECT_TRUE(DecideRespMonitor(&state, Request("https://api.test/v1"),
                                "text/html", std::string())
                  .monitor);
  EXPECT_EQ(DecideRespMonitor(&state, Request("https://api.test/v1"),
                              "text/html", std::string())
                .rule_id,
            "good");
}

TEST(RespThrottleTest, EachRuleCarriesItsOwnCaptureLimit) {
  // A `max_body_bytes` of 0 is "the rule did not state one", not "keep
  // nothing": the configuration layer resolves it to the ceiling. The rule
  // that does state one gets exactly that.
  fp::RespMonitorState state = Installed(R"({"enabled":true,"rules":[
    {"id":"silent","url_regex":"^https://api\\.test/whole",
     "max_body_bytes":0},
    {"id":"small","url_regex":"^https://api\\.test/small",
     "max_body_bytes":16}]})");

  const RespMonitorOutcome silent = DecideRespMonitor(
      &state, Request("https://api.test/whole"), std::string(), std::string());
  ASSERT_TRUE(silent.monitor);
  EXPECT_EQ(silent.capture_limit, fp::kRespMonitorMaxBodyBytesCeiling);

  const RespMonitorOutcome small = DecideRespMonitor(
      &state, Request("https://api.test/small"), std::string(), std::string());
  ASSERT_TRUE(small.monitor);
  EXPECT_EQ(small.capture_limit, 16u);

  // The limit is the rule's, and it is what the tee is given.
  fp::RespTee tee;
  fp::RespTeeReset(&tee, small.capture_limit);
  const std::string body = LongBody();
  std::string page;
  fp::RespTeeFan(&tee, body, &page);
  EXPECT_EQ(tee.capture.bytes.size(), 16u);
  EXPECT_EQ(tee.capture.bytes, body.substr(0, 16));
  EXPECT_TRUE(tee.capture.truncated);
  EXPECT_EQ(tee.capture.total_bytes, static_cast<std::uint64_t>(body.size()));
  // And the page still got everything, though the monitor kept only 16.
  EXPECT_EQ(page, body);
}

TEST(RespThrottleTest, TheBodyIsCarriedOnlyWhenTheRuleAskedForIt) {
  const std::string body = "{\"ok\":true}";

  fp::RespMonitorState quiet = Installed(kJsonSection);
  const RespMonitorOutcome off = DecideRespMonitor(
      &quiet, Request("https://api.test/v1"), "application/json",
      std::string());
  ASSERT_TRUE(off.monitor);
  fp::RespTee tee_off;
  fp::RespTeeReset(&tee_off, off.capture_limit);
  std::string page;
  fp::RespTeeFan(&tee_off, body, &page);
  // The monitor did keep the bytes - it needs them to count and to size the
  // capture - but the report of that response does not carry them.
  EXPECT_EQ(tee_off.capture.bytes, body);
  const fp::RespMonitorReport report_off =
      BuildRespMonitorReport(off, "https://api.test/v1", 200,
                             "application/json", tee_off);
  EXPECT_FALSE(report_off.include_body);
  EXPECT_TRUE(report_off.body.empty());
  EXPECT_EQ(report_off.body_bytes, static_cast<std::uint64_t>(body.size()));

  fp::RespMonitorState loud = Installed(R"({"enabled":true,
    "rules":[{"id":"api-json","url_regex":"^https://api\\.test/",
              "include_body":true,"max_body_bytes":64}]})");
  const RespMonitorOutcome on = DecideRespMonitor(
      &loud, Request("https://api.test/v1"), "application/json",
      std::string());
  ASSERT_TRUE(on.monitor);
  fp::RespTee tee_on;
  fp::RespTeeReset(&tee_on, on.capture_limit);
  fp::RespTeeFan(&tee_on, body, &page);
  const fp::RespMonitorReport report_on = BuildRespMonitorReport(
      on, "https://api.test/v1", 200, "application/json", tee_on);
  EXPECT_TRUE(report_on.include_body);
  EXPECT_EQ(report_on.body, body);
}

TEST(RespThrottleTest, TheSectionDefaultDecidesTheBodyForRulesThatDoNotSay) {
  // `include_body` in the section is the default for a rule that is silent,
  // and a rule that speaks overrides it.
  fp::RespMonitorState state = Installed(R"({"enabled":true,
    "include_body":true,
    "rules":[{"id":"silent","url_regex":"^https://api\\.test/a"},
             {"id":"quiet","url_regex":"^https://api\\.test/b",
              "include_body":false}]})");

  EXPECT_TRUE(DecideRespMonitor(&state, Request("https://api.test/a"),
                                std::string(), std::string())
                  .include_body);
  EXPECT_FALSE(DecideRespMonitor(&state, Request("https://api.test/b"),
                                 std::string(), std::string())
                   .include_body);
}

TEST(RespThrottleTest, ASwitchedOffMonitorHasNothingToMount) {
  // Present but off. Nothing is registered, so there is no rule to compile,
  // no capture buffer, and no throttle.
  EXPECT_FALSE(RespMonitorHasRules(ConfigFrom(
      R"({"enabled":false,"rules":[{"id":"a","url_regex":"x"}]})")));
  // On, but with nothing to claim.
  EXPECT_FALSE(RespMonitorHasRules(ConfigFrom(R"({"enabled":true})")));
  EXPECT_FALSE(RespMonitorHasRules(ConfigFrom(R"({"enabled":true,"rules":[]})")));
  // On, with a rule. That one is worth mounting.
  EXPECT_TRUE(RespMonitorHasRules(ConfigFrom(kJsonSection)));
}

TEST(RespThrottleTest, ASectionThatIsNotThereIsNotAnError) {
  // The ordinary "the platform did not ask for a monitor" case: the read
  // succeeds with a switched-off configuration, and nothing is mounted.
  fp::RespMonitorConfig config;
  ASSERT_TRUE(ReadRespMonitorConfig("{\"access_policy\":{}}", &config));
  EXPECT_FALSE(config.enabled);
  EXPECT_FALSE(RespMonitorHasRules(config));

  // A bundle that is not JSON at all is an error, and the output is left
  // switched off: a caller that ignores the return value still mounts
  // nothing rather than registering a monitor that was never asked for.
  fp::RespMonitorConfig broken;
  EXPECT_FALSE(ReadRespMonitorConfig("{not json", &broken));
  EXPECT_FALSE(RespMonitorHasRules(broken));
}

TEST(RespThrottleTest, TheReportCarriesTheResponseFactsAndNoRequestHeader) {
  fp::RespMonitorState state = Installed(kJsonSection);
  const RespMonitorOutcome outcome = DecideRespMonitor(
      &state, Request("https://api.test/v1"), "application/json; charset=utf-8",
      std::string());
  ASSERT_TRUE(outcome.monitor);
  fp::RespTee tee;
  fp::RespTeeReset(&tee, outcome.capture_limit);
  std::string page;
  fp::RespTeeFan(&tee, "{\"n\":1}", &page);

  const fp::RespMonitorReport report = BuildRespMonitorReport(
      outcome, "https://api.test/v1", 200, "application/json; charset=utf-8",
      tee);
  EXPECT_EQ(report.rule_id, "api-json");
  EXPECT_EQ(report.url, "https://api.test/v1");
  EXPECT_EQ(report.status, 200);
  EXPECT_EQ(report.content_type, "application/json; charset=utf-8");

  // What leaves the browser carries the response's own facts and nothing
  // that could carry a credential. A header is where credentials live, so a
  // header is what this list is about.
  const std::string payload = fp::BuildRespMonitorActionPayload(report);
  for (const char* forbidden :
       {"cookie", "set-cookie", "authorization", "headers", "proxy-"}) {
    EXPECT_EQ(payload.find(forbidden), std::string::npos)
        << "the report must not carry " << forbidden;
  }
  // The body was not asked for and did not travel.
  EXPECT_EQ(payload.find("\"body\""), std::string::npos);

  // And the payload satisfies the action the pipe files it under, or
  // EnqueueEvent rejects it and the observation is lost. No new action was
  // invented: the report rides the existing one.
  const fp::ActionCollectorPolicy collector;
  std::string action;
  const fp::ActionValidation validation = fp::ValidateActionPayload(
      fp::ActionTable::Get(), fp::kActionResponseMonitor, payload, collector,
      &action);
  EXPECT_TRUE(validation.ok);
  EXPECT_EQ(action, fp::kActionResponseMonitor);
  EXPECT_TRUE(fp::ActionTable::Get().Knows(fp::kActionResponseMonitor));
}

TEST(RespThrottleTest, TheBundlesResponseMonitorSectionIsWhatIsRead) {
  // The rules come from the ConfigBundle the runner sent and from nowhere
  // else - there is no second channel and no command-line switch.
  const fp::RespMonitorConfig config = ConfigFrom(R"({"enabled":true,
    "rules":[{"id":"from-bundle","url_regex":"^https://bundle\\.test/"}]})");
  ASSERT_EQ(config.rules.size(), 1u);
  EXPECT_EQ(config.rules[0].id, "from-bundle");

  // A section that belongs to another feature is not read as this one.
  fp::RespMonitorConfig other;
  ASSERT_TRUE(ReadRespMonitorConfig(
      "{\"access_policy\":{\"enabled\":true,\"rules\":[{\"id\":\"x\"}]}}",
      &other));
  EXPECT_TRUE(other.rules.empty());
}

}  // namespace
}  // namespace lengsuo